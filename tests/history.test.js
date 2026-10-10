import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest';

const telegram = require('../src/pkjs/telegram');
const history = require('../src/pkjs/history.js');
const Queue = require('../src/pkjs/lib/message_queue.js').Queue;

function dictBytes(msg) {
    let bytes = 1;
    for (const key of Object.keys(msg)) {
        const v = msg[key];
        bytes += 7 + (typeof v === 'string' ? Buffer.byteLength(v, 'utf8') + 1 : 4);
    }
    return bytes;
}

function own(id, text) { return { id, out: true, message: text }; }
function bot(id, text) { return { id, out: false, message: text }; }

describe('history transport', () => {
    let sent;
    let messages;
    let getMessages;

    beforeEach(() => {
        sent = [];
        global.Pebble.sendAppMessage = vi.fn((msg, ok) => { sent.push(msg); ok(); });
        Queue.reset();
        messages = [];
        getMessages = vi.fn(() => Promise.resolve(messages));
        vi.spyOn(telegram, 'hasSession').mockReturnValue(true);
        vi.spyOn(telegram, 'isAppConfigured').mockReturnValue(true);
        vi.spyOn(telegram, 'getBotUsername').mockReturnValue('@SquireTestBot');
        vi.spyOn(telegram, 'initClient').mockResolvedValue(true);
        vi.spyOn(telegram, 'getClient').mockReturnValue({
            getEntity: () => Promise.resolve({ className: 'User', bot: true, id: 777 }),
            getMessages,
        });
    });

    afterEach(() => {
        vi.useRealTimers();
        Queue.reset();
        vi.restoreAllMocks();
    });

    it('frames history with BEGIN and DONE, oldest first', async () => {
        messages = [
            bot(4, 'It is sunny. [done]'),
            own(3, '[thread:abc-123] <system>ctx</system>\n\nWeather?'),
            bot(2, 'Hello!'),
            own(1, 'Hi'),
        ];
        await history.fetchAndSendHistory();
        expect(sent).toEqual([
            { HISTORY_BEGIN: 1 },
            { HISTORY_THREAD_ID: 'abc-123' },
            { HISTORY_PROMPT: 'Hi' },
            { HISTORY_RESPONSE: 'Hello!' },
            { HISTORY_PROMPT: 'Weather?' },
            { HISTORY_RESPONSE: 'It is sunny.' },
            { HISTORY_DONE: 1 },
        ]);
    });

    it('bounds every entry to the watch buffer with valid UTF-8', async () => {
        // Regression: a 6000-byte reply was sent unsplit into a 5000-byte inbox.
        messages = [bot(2, 'x'.repeat(6000)), own(1, '日本'.repeat(2000)), bot(0, '😀'.repeat(3000))];
        await history.fetchAndSendHistory();
        for (const m of sent) {
            expect(dictBytes(m)).toBeLessThan(600);
            for (const v of Object.values(m)) {
                if (typeof v === 'string') {
                    expect(Buffer.byteLength(v, 'utf8')).toBeLessThanOrEqual(history.HISTORY_MAX_ENTRY_BYTES);
                    expect(Buffer.from(v, 'utf8').toString('utf8')).toBe(v);
                }
            }
        }
        expect(sent[sent.length - 1]).toEqual({ HISTORY_DONE: 1 });
    });

    it('reports a fetch failure explicitly', async () => {
        getMessages.mockRejectedValue(new Error('FLOOD_WAIT_10'));
        await history.fetchAndSendHistory();
        expect(sent).toEqual([{ HISTORY_BEGIN: 1 }, { HISTORY_DONE: 2 }]);
    });

    it('does not fetch from a guessed bot when none is configured', async () => {
        telegram.getBotUsername.mockReturnValue('');
        await history.fetchAndSendHistory();
        expect(getMessages).not.toHaveBeenCalled();
        expect(sent).toEqual([{ HISTORY_BEGIN: 1 }, { HISTORY_DONE: 1 }]);
    });

    it('drops a stale fetch superseded by a newer one', async () => {
        let resolveFirst;
        getMessages.mockImplementationOnce(() => new Promise(r => { resolveFirst = r; }));
        const first = history.fetchAndSendHistory();
        messages = [own(1, 'newer')];
        await new Promise(r => setTimeout(r, 0));
        const second = history.fetchAndSendHistory();
        await second;
        resolveFirst([own(1, 'older')]);
        await first;
        expect(sent.filter(m => m.HISTORY_BEGIN).length).toBe(1);
        expect(sent.some(m => m.HISTORY_PROMPT === 'older')).toBe(false);
        expect(sent.some(m => m.HISTORY_PROMPT === 'newer')).toBe(true);
    });

    it('times out a stalled fetch and ignores its eventual result', async () => {
        vi.useFakeTimers();
        let finishFetch;
        getMessages.mockImplementationOnce(() => new Promise(resolve => { finishFetch = resolve; }));
        const fetch = history.fetchAndSendHistory();
        await vi.advanceTimersByTimeAsync(60000);
        await fetch;
        expect(sent).toEqual([{ HISTORY_BEGIN: 1 }, { HISTORY_DONE: 2 }]);
        finishFetch([own(1, 'late result')]);
        for (let i = 0; i < 10; i++) await Promise.resolve();
        expect(sent).toHaveLength(2);
    });

    it('does not restore history when a fetch finishes after sign-out', async () => {
        let finishFetch;
        getMessages.mockImplementationOnce(() => new Promise(resolve => { finishFetch = resolve; }));
        const fetch = history.fetchAndSendHistory();
        for (let i = 0; i < 10; i++) await Promise.resolve();
        history.clearHistory();
        finishFetch([own(1, 'private old history')]);
        await fetch;
        expect(sent).toEqual([{ HISTORY_BEGIN: 1 }, { HISTORY_DONE: 1 }]);
    });

    it('clears a partial batch after an entry cannot be delivered', async () => {
        vi.useFakeTimers();
        messages = [bot(2, 'answer'), own(1, 'prompt')];
        global.Pebble.sendAppMessage = vi.fn((msg, ok, fail) => {
            if ('HISTORY_PROMPT' in msg) fail();
            else { sent.push(msg); ok(); }
        });
        await history.fetchAndSendHistory();
        await vi.runAllTimersAsync();
        expect(sent).toEqual([
            { HISTORY_BEGIN: 1 },
            { HISTORY_BEGIN: 1 },
            { HISTORY_DONE: 2 },
        ]);
        expect(Queue.size()).toBe(0);
    });

    it('does not revive an older thread after an untagged prompt', async () => {
        messages = [own(3, 'new conversation'), own(1, '[thread:old-thread] old prompt')];
        await history.fetchAndSendHistory();
        expect(sent.some(m => 'HISTORY_THREAD_ID' in m)).toBe(false);
    });

    it('ignores thread ids that are not well-formed', async () => {
        messages = [own(1, '[thread:../../etc] hi')];
        await history.fetchAndSendHistory();
        expect(sent.some(m => 'HISTORY_THREAD_ID' in m)).toBe(false);
    });
});
