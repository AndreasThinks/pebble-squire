import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest';

const fake = require('./helpers/fake_gramjs.js');
const appConfig = require('../src/pkjs/telegram/app_config.js');
const sessionStore = require('../src/pkjs/telegram/session.js');

const AUTH_PATH = require.resolve('../src/pkjs/telegram/auth.js');
const PHONE = '+15551234567';
const CODE = '24680';
const PASSWORD = 'correct horse';

function loadAuth() {
    return require(AUTH_PATH);
}

// Simulates PebbleKit JS being restarted: module state is lost, localStorage
// survives.
function restart() {
    delete require.cache[AUTH_PATH];
    return loadAuth();
}

function storedValues() {
    return localStorage.setItem.mock.calls.map(c => String(c[1]));
}

describe('Telegram sign-in', () => {
    let gram;
    let auth;

    beforeEach(() => {
        localStorage.clear();
        localStorage.setItem.mockClear();
        gram = fake.install(vi, {
            onInvoke(req) {
                if (req.className === 'auth.SendCode') {
                    return Promise.resolve({ phoneCodeHash: 'HASH-1', type: { className: 'auth.SentCodeTypeApp', length: 6 } });
                }
                if (req.className === 'auth.SignIn') {
                    if (req.phoneCode !== CODE) return Promise.reject(fake.telegramError('PHONE_CODE_INVALID'));
                    if (gram.twoFactor) return Promise.reject(fake.telegramError('SESSION_PASSWORD_NEEDED'));
                    return Promise.resolve({ className: 'auth.Authorization' });
                }
                return Promise.resolve({});
            },
        });
        auth = restart();
    });

    afterEach(() => {
        auth.clearPendingAuth();
        sessionStore.clearSession();
        fake.uninstall();
        vi.restoreAllMocks();
    });

    it('rejects malformed phone numbers without contacting Telegram', async () => {
        await expect(auth.startAuth('12345')).rejects.toMatchObject({ reason: auth.REASONS.INVALID_PHONE });
        expect(gram.instances.length).toBe(0);
    });

    it('survives a PebbleKit JS restart between phone and code steps', async () => {
        const result = await auth.startAuth(PHONE);
        expect(result).toMatchObject({ success: true, codeLength: 6 });
        expect(gram.instances[0].destroyed).toBe(true);

        auth = restart();
        expect(auth.getAuthState()).toMatchObject({ isWaitingForCode: true });
        await expect(auth.provideCode(CODE)).resolves.toMatchObject({ success: true });
        const signIn = gram.invocations.find(r => r.className === 'auth.SignIn');
        expect(signIn).toMatchObject({ phoneNumber: PHONE, phoneCodeHash: 'HASH-1' });
        expect(sessionStore.hasSession()).toBe(true);
        expect(localStorage.getItem('squire_pending_auth')).toBe(null);
        // Every short-lived client was torn down (no duplicate auth key use).
        expect(gram.instances.every(c => c.destroyed)).toBe(true);
    });

    it('cleans up the legacy waiting flag that was never usable', () => {
        localStorage.setItem('telegram_auth_waiting_for_code', 'true');
        auth = restart();
        expect(auth.getAuthState().isWaitingForCode).toBe(false);
        expect(localStorage.getItem('telegram_auth_waiting_for_code')).toBe(null);
    });

    it('lets the user retry a wrong code', async () => {
        await auth.startAuth(PHONE);
        await expect(auth.provideCode('11111')).rejects.toMatchObject({ reason: auth.REASONS.INVALID_CODE });
        await expect(auth.provideCode(CODE)).resolves.toMatchObject({ success: true });
    });

    it('expires an abandoned sign-in', async () => {
        await auth.startAuth(PHONE);
        const pending = JSON.parse(localStorage.getItem('squire_pending_auth'));
        pending.expiresAt = Date.now() - 1;
        localStorage.setItem('squire_pending_auth', JSON.stringify(pending));
        auth = restart();
        await expect(auth.provideCode(CODE)).rejects.toMatchObject({ reason: auth.REASONS.NO_PENDING });
        expect(localStorage.getItem('squire_pending_auth')).toBe(null);
    });

    it('completes two-step verification across a restart, never storing code or password', async () => {
        gram.twoFactor = true;
        await auth.startAuth(PHONE);
        await expect(auth.provideCode(CODE)).resolves.toMatchObject({ status: 'password_needed' });
        auth = restart();
        expect(auth.getAuthState().isWaitingForPassword).toBe(true);
        await expect(auth.providePassword(PASSWORD)).resolves.toMatchObject({ success: true });
        expect(sessionStore.hasSession()).toBe(true);
        for (const value of storedValues()) {
            expect(value).not.toContain(CODE);
            expect(value).not.toContain(PASSWORD);
        }
    });

    it('stops after one wrong 2FA password instead of looping', async () => {
        // Regression: onError returned nothing, so GramJS kept re-submitting
        // the same wrong password.
        gram.twoFactor = true;
        await auth.startAuth(PHONE);
        await auth.provideCode(CODE);
        await expect(auth.providePassword('wrong')).rejects.toMatchObject({ reason: auth.REASONS.BAD_PASSWORD });
        expect(gram.passwordAttempts).toBe(1);
        // The pending sign-in survives so the right password can be entered.
        await expect(auth.providePassword(PASSWORD)).resolves.toMatchObject({ success: true });
    });

    it('reports an unconfigured build distinctly', async () => {
        vi.spyOn(appConfig, 'getCredentials').mockImplementation(() => {
            throw Object.assign(new Error('not configured'), { code: 'APP_NOT_CONFIGURED' });
        });
        await expect(auth.startAuth(PHONE)).rejects.toMatchObject({ reason: auth.REASONS.APP_NOT_CONFIGURED });
    });

    it('maps flood waits to an actionable reason', async () => {
        gram.onInvoke = () => Promise.reject(fake.telegramError('FLOOD_WAIT_300'));
        await expect(auth.startAuth(PHONE)).rejects.toMatchObject({ reason: auth.REASONS.FLOOD_WAIT });
        expect(gram.instances.every(c => c.destroyed)).toBe(true);
    });
});
