import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest';

const queueModule = require('../src/pkjs/lib/message_queue.js');
const Queue = queueModule.Queue;

function utf8(str) {
    return Buffer.byteLength(str, 'utf8');
}

// Dictionary size as the watch sees it: 1 + per tuple (7 + value).
function dictBytes(msg) {
    let bytes = 1;
    for (const key of Object.keys(msg)) {
        const v = msg[key];
        bytes += 7 + (typeof v === 'string' ? utf8(v) + 1 : 4);
    }
    return bytes;
}

describe('MessageQueue', () => {
    let sent;
    let raw;
    let behaviour;

    beforeEach(() => {
        vi.useFakeTimers();
        sent = [];
        raw = [];
        behaviour = () => 'ok';
        global.Pebble.sendAppMessage = vi.fn((msg, success, failure) => {
            raw.push(msg);
            sent.push(stripSeq(msg));
            const result = behaviour(msg, sent.length);
            if (result === 'ok') success();
            else if (result === 'fail') failure();
            // 'hang': never call back
        });
        Queue.reset();
    });

    afterEach(() => {
        Queue.reset();
        vi.useRealTimers();
    });

    it('sends a small CHAT in one message', () => {
        Queue.enqueue({ CHAT: 'hello' });
        expect(sent).toEqual([{ CHAT: 'hello' }]);
    });

    it('keeps exactly one message in flight', () => {
        behaviour = () => 'hang';
        Queue.enqueue({ CHAT: 'a' });
        Queue.enqueue({ CHAT: 'b' });
        expect(sent.length).toBe(1);
    });

    it('never lets CHAT_DONE overtake a CHAT that is being retried', async () => {
        // Regression: the in-flight slot was released during retry backoff,
        // so CHAT_DONE was delivered before the failed CHAT.
        behaviour = (msg, n) => (n === 1 ? 'fail' : 'ok');
        Queue.enqueue({ CHAT: 'part one' });
        Queue.enqueue({ CHAT_DONE: 1 });
        await vi.runAllTimersAsync();
        expect(sent.map(m => Object.keys(m)[0])).toEqual(['CHAT', 'CHAT', 'CHAT_DONE']);
    });

    it('bounds retries and reports a terminal failure for the group', async () => {
        behaviour = (msg) => (msg.CHAT === 'doomed' ? 'fail' : 'ok');
        const onFailed = vi.fn();
        Queue.enqueue({ CHAT: 'doomed' }, { group: 'g', onFailed });
        Queue.enqueue({ CHAT: 'rest of reply' }, { group: 'g', onFailed });
        Queue.enqueue({ CHAT_DONE: 1 }, { group: 'g', onFailed });
        Queue.enqueue({ HISTORY_DONE: 1 });
        await vi.runAllTimersAsync();
        expect(sent.filter(m => m.CHAT === 'doomed').length).toBe(queueModule.MAX_SEND_ATTEMPTS);
        // The rest of the failed reply is dropped, never shown after a hole.
        expect(sent.some(m => m.CHAT === 'rest of reply')).toBe(false);
        expect(sent.some(m => m.CHAT_DONE)).toBe(false);
        expect(onFailed).toHaveBeenCalledTimes(1);
        // Unrelated traffic still flows: no indefinite blockage.
        expect(sent[sent.length - 1]).toEqual({ HISTORY_DONE: 1 });
        // Terminal messages for a failed group are still allowed.
        Queue.enqueue({ WARNING: 'lost', CHAT_DONE: 1 }, { group: 'g', terminal: true });
        Queue.enqueue({ CHAT: 'late non-terminal' }, { group: 'g' });
        await vi.runAllTimersAsync();
        expect(sent[sent.length - 1]).toEqual({ WARNING: 'lost', CHAT_DONE: 1 });
    });

    it('treats a send that never calls back as failed and retries it', async () => {
        behaviour = (msg, n) => (n === 1 ? 'hang' : 'ok');
        Queue.enqueue({ CHAT: 'x' });
        Queue.enqueue({ CHAT_DONE: 1 });
        await vi.runAllTimersAsync();
        expect(sent.map(m => Object.keys(m)[0])).toEqual(['CHAT', 'CHAT', 'CHAT_DONE']);
    });

    it('ignores a late callback from a timed-out attempt', async () => {
        const callbacks = [];
        global.Pebble.sendAppMessage = vi.fn((msg, success) => {
            sent.push(stripSeq(msg));
            callbacks.push(success);
            if (sent.length > 1) success();
        });
        Queue.enqueue({ CHAT: 'a' });
        Queue.enqueue({ CHAT: 'b' });
        await vi.advanceTimersByTimeAsync(16000);
        callbacks[0]();  // the first attempt finally acks, after the retry
        await vi.runAllTimersAsync();
        expect(sent.map(m => m.CHAT)).toEqual(['a', 'a', 'b']);
    });

    describe('ambiguous delivery', () => {
        // A minimal model of the watch: applies CHAT text unless the message
        // repeats the SEQ it applied last (src/c/util/transport.c).
        function watchModel() {
            const model = { text: '', lastSeq: null };
            model.receive = (msg) => {
                if (msg.SEQ !== undefined && msg.SEQ === model.lastSeq) return;
                if (msg.SEQ !== undefined) model.lastSeq = msg.SEQ;
                if (typeof msg.CHAT === 'string') model.text += msg.CHAT;
            };
            return model;
        }

        it('resends with the same SEQ after a lost ack, so the watch can drop the copy', async () => {
            const watch = watchModel();
            const callbacks = [];
            global.Pebble.sendAppMessage = vi.fn((msg, success) => {
                raw.push(msg);
                watch.receive(msg);          // every attempt is delivered...
                callbacks.push(success);
                if (raw.length > 1) success(); // ...but the first ack is lost
            });
            Queue.enqueue({ CHAT: 'Hello ' }, { group: 'g' });
            Queue.enqueue({ CHAT: 'world' }, { group: 'g' });
            await vi.advanceTimersByTimeAsync(16000);
            callbacks[0]();                  // the lost ack turns up late
            await vi.runAllTimersAsync();
            expect(raw.map(m => m.CHAT)).toEqual(['Hello ', 'Hello ', 'world']);
            expect(raw[0].SEQ).toBe(raw[1].SEQ);
            expect(raw[2].SEQ).not.toBe(raw[1].SEQ);
            expect(watch.text).toBe('Hello world');
        });

        it('gives every message and every chunk its own SEQ, within the inbox size', () => {
            Queue.enqueue({ CHAT: 'x'.repeat(9000), REQUEST_ID: 3 });
            Queue.enqueue({ CHAT_DONE: 1 });
            const seqs = raw.map(m => m.SEQ);
            expect(seqs.every(n => Number.isInteger(n) && n > 0)).toBe(true);
            expect(new Set(seqs).size).toBe(seqs.length);
            for (const m of raw) expect(dictBytes(m)).toBeLessThanOrEqual(5000);
        });
    });

    describe('budget', () => {
        it('rejects a group explicitly when the queue is full, then recovers', async () => {
            behaviour = () => 'hang';
            const onFailed = vi.fn();
            Queue.enqueue({ CHAT: 'head' }, { group: 'a' });
            let accepted = 0;
            for (let i = 0; i < 1000; i++) {
                if (!Queue.enqueue({ CHAT: 'y'.repeat(400) }, { group: 'b', onFailed })) break;
                accepted++;
            }
            expect(accepted).toBeLessThan(1000);
            expect(Queue.size()).toBeLessThanOrEqual(queueModule.MAX_QUEUE_ITEMS);
            expect(Queue.bytes()).toBeLessThanOrEqual(queueModule.MAX_QUEUE_BYTES);
            expect(onFailed).toHaveBeenCalledTimes(1);
            // The failed group's queued messages were dropped to make room.
            expect(Queue.size()).toBe(1);
            // A terminal report for the failed group still gets through.
            expect(Queue.enqueue({ WARNING: 'too long', CHAT_DONE: 1 }, { group: 'b', terminal: true })).toBe(true);
            // The in-flight head is untouched and completes normally.
            behaviour = () => 'ok';
            await vi.advanceTimersByTimeAsync(16000);
            await vi.runAllTimersAsync();
            expect(sent.filter(m => m.CHAT === 'head').length).toBe(2);
            expect(sent[sent.length - 1]).toEqual({ WARNING: 'too long', CHAT_DONE: 1 });
            expect(Queue.size()).toBe(0);
            expect(Queue.enqueue({ CHAT: 'after recovery' }, { group: 'c' })).toBe(true);
            expect(sent[sent.length - 1]).toEqual({ CHAT: 'after recovery' });
        });

        it('drops ungrouped messages over budget with an explicit false', () => {
            behaviour = () => 'hang';
            let results = [];
            for (let i = 0; i < queueModule.MAX_QUEUE_ITEMS + 5; i++) {
                results.push(Queue.enqueue({ HISTORY_PROMPT: 'p' + i }));
            }
            expect(results.filter(r => r === false).length).toBeGreaterThan(0);
            expect(Queue.size()).toBeLessThanOrEqual(queueModule.MAX_QUEUE_ITEMS);
        });

        it('never splits a message across the budget: all chunks or none', () => {
            behaviour = () => 'hang';
            const onFailed = vi.fn();
            Queue.enqueue({ CHAT: 'z'.repeat(queueModule.MAX_QUEUE_BYTES - 6000) }, { group: 'a' });
            const before = Queue.size();
            expect(Queue.enqueue({ CHAT: 'w'.repeat(12000) }, { group: 'b', onFailed })).toBe(false);
            expect(Queue.size()).toBe(before);
            expect(onFailed).toHaveBeenCalledTimes(1);
        });
    });

    it('purges queued messages of a cancelled group', () => {
        behaviour = () => 'hang';
        Queue.enqueue({ CHAT: 'in flight' }, { group: 'old' });
        Queue.enqueue({ CHAT: 'queued' }, { group: 'old' });
        Queue.enqueue({ CHAT: 'other' }, { group: 'new' });
        Queue.purgeGroup('old');
        expect(Queue.size()).toBe(2);
    });

    it('chunks a large CHAT so every message fits the 5000-byte inbox', () => {
        const big = 'x'.repeat(10000);
        Queue.enqueue({ CHAT: big, REQUEST_ID: 42 });
        expect(sent.length).toBeGreaterThan(2);
        expect(sent.map(m => m.CHAT).join('')).toBe(big);
        for (const m of sent) {
            expect(m.REQUEST_ID).toBe(42);
            expect(dictBytes(m)).toBeLessThanOrEqual(5000);
        }
    });

    it('measures UTF-8 bytes including tuple overhead for multibyte text', () => {
        const big = '日本語テキスト😀'.repeat(700);
        Queue.enqueue({ CHAT: big, REQUEST_ID: 7 });
        let joined = '';
        for (const m of sent) {
            expect(dictBytes(m)).toBeLessThanOrEqual(5000);
            expect(utf8(m.CHAT)).toBeLessThanOrEqual(4000);
            // Each chunk is well-formed UTF-16 (no split surrogate pair).
            expect(Buffer.from(m.CHAT, 'utf8').toString('utf8')).toBe(m.CHAT);
            joined += m.CHAT;
        }
        expect(joined).toBe(big);
    });

    it('puts other keys on the last chunk only', () => {
        Queue.enqueue({ CHAT: 'y'.repeat(9000), CHAT_DONE: 1, REQUEST_ID: 3 });
        const last = sent[sent.length - 1];
        expect(last.CHAT_DONE).toBe(1);
        for (const m of sent.slice(0, -1)) {
            expect(m.CHAT_DONE).toBeUndefined();
        }
    });

    it('replaces lone surrogates and strips NUL so the C string survives', () => {
        Queue.enqueue({ CHAT: 'a\uD83Db\u0000c' });
        expect(sent[0].CHAT).toBe('a�bc');
    });

    it('refuses a non-chunkable message that cannot fit instead of sending it to be dropped', async () => {
        const onFailed = vi.fn();
        Queue.enqueue({ WARNING: 'w'.repeat(6000) }, { group: 'g', onFailed });
        await vi.runAllTimersAsync();
        expect(sent.length).toBe(0);
        expect(onFailed).toHaveBeenCalledTimes(1);
    });

    it('truncateUtf8 cuts on a character boundary', () => {
        const text = 'é'.repeat(400);
        const cut = queueModule.truncateUtf8(text, 101);
        expect(utf8(cut)).toBeLessThanOrEqual(101);
        expect(cut.endsWith('…')).toBe(true);
        expect(Buffer.from(cut, 'utf8').toString('utf8')).toBe(cut);
    });
});
