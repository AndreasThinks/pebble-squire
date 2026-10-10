import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest';

const fake = require('./helpers/fake_gramjs.js');
const client = require('../src/pkjs/telegram/client.js');
const sessionStore = require('../src/pkjs/telegram/session.js');
const Queue = require('../src/pkjs/lib/message_queue.js').Queue;

describe('Telegram client lifecycle', () => {
    let gram;
    let sent;

    beforeEach(async () => {
        localStorage.clear();
        sent = [];
        global.Pebble.sendAppMessage = vi.fn((msg, ok) => { sent.push(msg); ok(); });
        Queue.reset();
        gram = fake.install(vi, {});
        await client.resetClient();
        sessionStore.saveSession('stored-session');
    });

    afterEach(async () => {
        await client.resetClient();
        fake.uninstall();
        vi.restoreAllMocks();
        Queue.reset();
    });

    it('shares one connection between concurrent callers', async () => {
        // Two connections on one auth key make Telegram revoke it.
        await Promise.all([client.initClient(), client.initClient(), client.initClient()]);
        expect(gram.instances.length).toBe(1);
        await client.initClient();
        expect(gram.instances.length).toBe(1);
    });

    it('refuses a runtime without secure randomness before connecting', async () => {
        const source = global.crypto.getRandomValues;
        global.crypto.getRandomValues = () => { throw new Error('unavailable'); };
        try {
            await expect(client.initClient()).rejects.toMatchObject({ code: 'SECURE_RANDOM_UNAVAILABLE' });
            expect(gram.instances).toHaveLength(0);
            expect(sessionStore.hasSession()).toBe(true);
        } finally {
            global.crypto.getRandomValues = source;
        }
    });

    it('tears the old client down when reset', async () => {
        await client.initClient();
        await client.resetClient();
        expect(gram.instances[0].destroyed).toBe(true);
    });

    it('does not publish a connection that finishes after reset', async () => {
        let authorize;
        vi.spyOn(global.TelegramClient.prototype, 'isUserAuthorized').mockImplementation(
            () => new Promise(resolve => { authorize = resolve; })
        );
        const connecting = client.initClient();
        const rejected = expect(connecting).rejects.toThrow(/cancelled/);
        for (let i = 0; i < 12; i++) await Promise.resolve();
        expect(authorize).toBeTypeOf('function');
        await client.resetClient();
        authorize(true);
        await rejected;
        for (let i = 0; i < 12; i++) await Promise.resolve();
        expect(client.getClient()).toBe(null);
        expect(client.isClientConnected()).toBe(false);
        expect(gram.instances[0].destroyed).toBe(true);
    });

    it('waits for old-client teardown before reconnecting with the same key', async () => {
        await client.initClient();
        const old = gram.instances[0];
        let finishClosing;
        old.destroy = vi.fn(() => new Promise(resolve => {
            finishClosing = () => { old.connected = false; old.destroyed = true; resolve(); };
        }));
        old.connected = false;
        const reconnect = client.initClient();
        for (let i = 0; i < 12; i++) await Promise.resolve();
        expect(gram.instances).toHaveLength(1);
        finishClosing();
        await reconnect;
        expect(gram.instances).toHaveLength(2);
        expect(old.destroyed).toBe(true);
    });

    it('clears a session Telegram has revoked and tells the watch', async () => {
        fake.uninstall();
        gram = fake.install(vi, { connectError: fake.telegramError('AUTH_KEY_UNREGISTERED') });
        await expect(client.initClient()).rejects.toThrow(/re-authenticate/);
        expect(sessionStore.hasSession()).toBe(false);
        expect(sent).toContainEqual({ TELEGRAM_CONNECTED: 0 });
    });

    it('reports server-side revocation on logout', async () => {
        gram.onInvoke = () => Promise.resolve(true);
        await client.initClient();
        await expect(client.disconnect()).resolves.toEqual({ revoked: true });
        expect(gram.invocations.some(r => r.className === 'auth.LogOut')).toBe(true);
        expect(sessionStore.hasSession()).toBe(false);
    });

    it('reports a local-only sign-out when revocation fails', async () => {
        gram.onInvoke = () => Promise.reject(new Error('Not connected'));
        await client.initClient();
        await expect(client.disconnect()).resolves.toEqual({ revoked: false });
        expect(sessionStore.hasSession()).toBe(false);
    });

    it('clears local sign-in when the server logout RPC never settles', async () => {
        vi.useFakeTimers();
        try {
            gram.onInvoke = () => new Promise(() => {});
            await client.initClient();
            const logout = client.disconnect();
            await vi.advanceTimersByTimeAsync(30000);
            await expect(logout).resolves.toEqual({ revoked: false });
            expect(sessionStore.hasSession()).toBe(false);
            expect(gram.instances[0].destroyed).toBe(true);
        } finally {
            vi.useRealTimers();
        }
    });

    it('reports a local-only sign-out when Telegram is unreachable', async () => {
        fake.uninstall();
        gram = fake.install(vi, { connectError: new Error('Connection to Telegram timed out') });
        await expect(client.disconnect()).resolves.toEqual({ revoked: false });
        expect(sessionStore.hasSession()).toBe(false);
    });
});

describe('bot username', () => {
    beforeEach(() => localStorage.clear());

    it('has no default bot', () => {
        expect(sessionStore.getBotUsername()).toBe('');
    });

    it('normalises and validates the configured bot', () => {
        localStorage.setItem('clay-settings', JSON.stringify({ AGENT_TELEGRAM_USERNAME: ' HermesHelperBot ' }));
        expect(sessionStore.getBotUsername()).toBe('@HermesHelperBot');
        localStorage.setItem('clay-settings', JSON.stringify({ AGENT_TELEGRAM_USERNAME: 'not a username!' }));
        expect(sessionStore.getBotUsername()).toBe('');
    });

    it('is not overridden by the legacy cached key', () => {
        localStorage.setItem('agent_telegram_username', '@OldBot');
        localStorage.setItem('clay-settings', JSON.stringify({ AGENT_TELEGRAM_USERNAME: '@NewAgentBot' }));
        expect(sessionStore.getBotUsername()).toBe('@NewAgentBot');
    });
});
