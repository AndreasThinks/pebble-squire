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

    it('tears the old client down when reset', async () => {
        await client.initClient();
        await client.resetClient();
        expect(gram.instances[0].destroyed).toBe(true);
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
