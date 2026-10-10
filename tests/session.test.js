import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest';

const telegram = require('../src/pkjs/telegram');
const sessionModule = require('../src/pkjs/session.js');
const Queue = require('../src/pkjs/lib/message_queue.js').Queue;
const { NewMessage, Raw, makeFakeClient, botMessage, peerUser, peerChat } = require('./helpers/fake_telegram.js');

const REQUEST_ID = 4242;

async function flush() {
    for (let i = 0; i < 10; i++) {
        await Promise.resolve();
    }
}

describe('Session', () => {
    let client;
    let sent;
    let botUsername;

    function setup(options) {
        client = makeFakeClient(options);
        vi.spyOn(telegram, 'getClient').mockImplementation(() => client);
    }

    function start(prompt) {
        return sessionModule.startSession(prompt || 'What is the weather?', null, REQUEST_ID);
    }

    function watchMessages() {
        return sent.map(m => {
            const copy = Object.assign({}, m);
            delete copy.REQUEST_ID;
            return copy;
        });
    }

    function chatText() {
        let text = '';
        for (const m of sent) {
            if (m.CHAT_RESET) text = '';
            if (typeof m.CHAT === 'string') text += m.CHAT;
        }
        return text;
    }

    function doneCount() {
        return sent.filter(m => m.CHAT_DONE).length;
    }

    beforeEach(() => {
        vi.useFakeTimers();
        sent = [];
        global.Pebble.sendAppMessage = vi.fn((msg, ok) => { sent.push(msg); ok(); });
        Queue.reset();
        botUsername = '@SquireTestBot';
        localStorage.clear();
        vi.spyOn(telegram, 'hasSession').mockReturnValue(true);
        vi.spyOn(telegram, 'isAppConfigured').mockReturnValue(true);
        vi.spyOn(telegram, 'getBotUsername').mockImplementation(() => botUsername);
        vi.spyOn(telegram, 'initClient').mockResolvedValue(true);
        vi.spyOn(telegram, 'getEventBuilders').mockReturnValue({ NewMessage, Raw, EditedMessage: null });
        setup();
    });

    afterEach(() => {
        sessionModule.cancelActiveSession();
        Queue.reset();
        vi.restoreAllMocks();
        vi.useRealTimers();
    });

    describe('peer filtering and correlation', () => {
        it('ignores an unrelated incoming DM and group message', async () => {
            // Regression: NewMessage({incoming:true}) accepted any chat.
            start();
            await flush();
            client.emitNewMessage({ id: 101, message: 'Private unrelated DM', out: false, peerId: peerUser('999'), senderId: { toString: () => '999' } });
            client.emitNewMessage({ id: 102, message: 'Group chatter', out: false, peerId: peerChat('555'), senderId: { toString: () => '777' } });
            client.emitNewMessage(botMessage(103, 'Sunny.'));
            expect(chatText()).toBe('Sunny.');
        });

        it('rejects a message in the bot chat claiming another sender', async () => {
            start();
            await flush();
            client.emitNewMessage(botMessage(101, 'spoof', { senderId: { toString: () => '999' } }));
            expect(chatText()).toBe('');
        });

        it('registers listeners before sending the prompt', async () => {
            start();
            await flush();
            expect(client.calls.indexOf('addEventHandler')).toBeLessThan(client.calls.indexOf('sendMessage'));
        });

        it('accepts a reply that arrives before sendMessage resolves, but not older messages', async () => {
            setup({ deferSend: true, sentId: 200 });
            start();
            await flush();
            client.emitNewMessage(botMessage(150, 'reply to an older prompt'));
            client.emitNewMessage(botMessage(201, 'fast reply'));
            expect(chatText()).toBe('');
            client.resolveSend(200);
            await flush();
            expect(chatText()).toBe('fast reply');
        });

        it('ignores a bot message that explicitly replies to a different prompt', async () => {
            start();
            await flush();
            client.emitNewMessage(botMessage(101, 'answer to message 90', { replyTo: { replyToMsgId: 90 } }));
            client.emitNewMessage(botMessage(102, 'answer to ours', { replyTo: { replyToMsgId: 100 } }));
            expect(chatText()).toBe('answer to ours');
        });

        it('tags every watch message with the request id', async () => {
            start();
            await flush();
            client.typing();
            client.emitNewMessage(botMessage(101, 'Hi [done]'));
            expect(sent.length).toBeGreaterThan(0);
            for (const m of sent) expect(m.REQUEST_ID).toBe(REQUEST_ID);
        });
    });

    it('times out a send that never settles and ignores its eventual reply', async () => {
        setup({ deferSend: true, sentId: 200 });
        const session = start();
        await flush();
        await vi.advanceTimersByTimeAsync(sessionModule.HARD_TIMEOUT_MS);
        await expect(session.done).resolves.toMatchObject({ complete: false });
        expect(doneCount()).toBe(1);
        expect(client.handlers).toHaveLength(0);
        client.resolveSend(200);
        await flush();
        client.emitNewMessage(botMessage(201, 'too late'));
        expect(chatText()).toBe('');
        expect(doneCount()).toBe(1);
    });

    describe('bot configuration', () => {
        it('never sends when no bot is configured (no @MyAgentBot default)', async () => {
            botUsername = '';
            start();
            await flush();
            expect(client.calls).not.toContain('sendMessage');
            expect(sent.some(m => m.WARNING && /bot/i.test(m.WARNING))).toBe(true);
            expect(doneCount()).toBe(1);
        });

        it('fails safely when the bot cannot be resolved', async () => {
            setup({ entityError: Object.assign(new Error('x'), { errorMessage: 'USERNAME_NOT_OCCUPIED' }) });
            start();
            await flush();
            expect(client.calls).not.toContain('sendMessage');
            expect(client.handlers.length).toBe(0);
            expect(sent.some(m => m.WARNING && m.WARNING.indexOf("Couldn't find @SquireTestBot") === 0)).toBe(true);
            expect(doneCount()).toBe(1);
        });

        it('refuses to send prompts to a non-bot user', async () => {
            setup({ entity: { className: 'User', id: { toString: () => '777' }, bot: false } });
            start();
            await flush();
            expect(client.calls).not.toContain('sendMessage');
            expect(doneCount()).toBe(1);
        });

        it('reports an unconfigured build instead of connecting', async () => {
            telegram.isAppConfigured.mockReturnValue(false);
            start();
            await flush();
            expect(telegram.initClient).not.toHaveBeenCalled();
            expect(sent.some(m => m.WARNING && /credentials/.test(m.WARNING))).toBe(true);
        });
    });

    describe('completion policy', () => {
        it('does not complete two seconds after a progress reply', async () => {
            // Regression: a fixed 2s timer ended the conversation after
            // "Let me check..." even though the agent was still working.
            start();
            await flush();
            client.emitNewMessage(botMessage(101, 'Let me check...'));
            await vi.advanceTimersByTimeAsync(5000);
            expect(doneCount()).toBe(0);
            client.emitNewMessage(botMessage(102, 'It is sunny.'));
            expect(chatText()).toBe('Let me check...\n\nIt is sunny.');
            await vi.advanceTimersByTimeAsync(sessionModule.QUIET_COMPLETE_MS + 10);
            expect(doneCount()).toBe(1);
        });

        it('keeps waiting while the bot is typing', async () => {
            start();
            await flush();
            client.emitNewMessage(botMessage(101, 'Working on it'));
            for (let i = 0; i < 6; i++) {
                await vi.advanceTimersByTimeAsync(5000);
                client.typing();
            }
            expect(doneCount()).toBe(0);
            await vi.advanceTimersByTimeAsync(sessionModule.QUIET_COMPLETE_MS + 10);
            expect(doneCount()).toBe(1);
        });

        it('completes immediately on [done] and the marker never races a timer', async () => {
            start();
            await flush();
            client.emitNewMessage(botMessage(101, 'Part one'));
            await vi.advanceTimersByTimeAsync(9000);
            client.emitNewMessage(botMessage(102, 'Part two [done]'));
            expect(chatText()).toBe('Part one\n\nPart two');
            expect(doneCount()).toBe(1);
            expect(client.handlers.length).toBe(0);
            await vi.runAllTimersAsync();
            expect(doneCount()).toBe(1);
        });

        it('trusts a bot that has used [done] to keep using it', async () => {
            start();
            await flush();
            client.emitNewMessage(botMessage(101, 'first answer [done]'));
            await flush();
            sent.length = 0;
            setup({ sentId: 300 });
            start('second question');
            await flush();
            client.emitNewMessage(botMessage(301, 'Looking that up'));
            await vi.advanceTimersByTimeAsync(30000);
            expect(doneCount()).toBe(0);
            client.emitNewMessage(botMessage(302, 'Found it. [done]'));
            expect(doneCount()).toBe(1);
        });

        it('falls back to the quiet window in marker mode, bounded', async () => {
            localStorage.setItem('squire_agent_uses_done_marker', '@squiretestbot');
            start();
            await flush();
            client.emitNewMessage(botMessage(101, 'forgot the marker'));
            await vi.advanceTimersByTimeAsync(sessionModule.MARKER_MODE_QUIET_MS + 10);
            expect(doneCount()).toBe(1);
        });

        it('gives up with a warning when the agent never replies', async () => {
            start();
            await flush();
            await vi.advanceTimersByTimeAsync(sessionModule.FIRST_REPLY_TIMEOUT_MS + 10);
            expect(sent.some(m => m.WARNING && /No reply/.test(m.WARNING))).toBe(true);
            expect(doneCount()).toBe(1);
            expect(client.handlers.length).toBe(0);
        });

        it('has a hard limit even with endless typing', async () => {
            start();
            await flush();
            client.emitNewMessage(botMessage(101, 'thinking'));
            for (let t = 0; t < sessionModule.HARD_TIMEOUT_MS; t += 5000) {
                client.typing();
                await vi.advanceTimersByTimeAsync(5000);
            }
            expect(doneCount()).toBe(1);
            expect(sent.some(m => m.WARNING && /Stopped waiting/.test(m.WARNING))).toBe(true);
        });
    });

    describe('edits', () => {
        it('sends only the appended text for streaming edits', async () => {
            start();
            await flush();
            client.emitNewMessage(botMessage(101, 'The weather'));
            client.edit(botMessage(101, 'The weather is'));
            client.edit(botMessage(101, 'The weather is sunny.'));
            client.edit(botMessage(101, 'The weather is sunny.'));
            expect(watchMessages().filter(m => m.CHAT !== undefined).map(m => m.CHAT)).toEqual(['The weather', ' is', ' sunny.']);
            expect(chatText()).toBe('The weather is sunny.');
        });

        it('replaces the reply when an edit rewrites earlier text', async () => {
            start();
            await flush();
            client.emitNewMessage(botMessage(101, 'Draft answer'));
            client.edit(botMessage(101, 'Final answer [done]'));
            expect(sent.some(m => m.CHAT_RESET)).toBe(true);
            expect(chatText()).toBe('Final answer');
            expect(doneCount()).toBe(1);
        });

        it('ignores edits to messages from before the prompt', async () => {
            start();
            await flush();
            client.edit(botMessage(50, 'old message edited'));
            expect(chatText()).toBe('');
        });
    });

    describe('lifecycle', () => {
        it('cancels the previous request when a new prompt arrives', async () => {
            const first = start('first');
            await flush();
            const firstClient = client;
            client.emitNewMessage(botMessage(101, 'first reply'));
            setup({ sentId: 500 });
            const second = sessionModule.startSession('second', null, REQUEST_ID + 1);
            await flush();
            expect(firstClient.handlers.length).toBe(0);
            // Nothing more from the first request reaches the watch.
            firstClient.emitNewMessage(botMessage(102, 'late first reply'));
            await vi.advanceTimersByTimeAsync(sessionModule.HARD_TIMEOUT_MS);
            const firstDone = sent.filter(m => m.REQUEST_ID === REQUEST_ID && m.CHAT_DONE);
            expect(firstDone.length).toBe(0);
            expect(await first.done).toMatchObject({ cancelled: true });
            expect(second).not.toBe(first);
        });

        it('removes every handler and timer on completion', async () => {
            start();
            await flush();
            client.typing();
            client.emitNewMessage(botMessage(101, 'answer [done]'));
            await flush();
            expect(client.handlers.length).toBe(0);
            expect(vi.getTimerCount()).toBe(0);
            expect(sessionModule.getActiveSession()).toBe(null);
        });

        it('removes every handler and timer on error', async () => {
            client.sendMessage = () => Promise.reject(Object.assign(new Error('x'), { errorMessage: 'YOU_BLOCKED_USER' }));
            start();
            await flush();
            expect(client.handlers.length).toBe(0);
            expect(vi.getTimerCount()).toBe(0);
            expect(doneCount()).toBe(1);
        });

        it('stops and reports when part of the reply cannot reach the watch', async () => {
            start();
            await flush();
            // The reply text can't get through (e.g. it keeps being dropped),
            // but small control messages still can.
            global.Pebble.sendAppMessage = vi.fn((msg, ok, fail) => {
                sent.push(msg);
                if (msg.CHAT !== undefined) fail(); else ok();
            });
            client.emitNewMessage(botMessage(101, 'this chunk is lost'));
            await vi.runAllTimersAsync();
            expect(client.handlers.length).toBe(0);
            const last = sent.slice(-2);
            expect(last[0].WARNING).toMatch(/couldn't be delivered/);
            expect(last[1].CHAT_DONE).toBe(1);
        });
    });
});
