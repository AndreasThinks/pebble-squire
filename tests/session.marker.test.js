import { describe, it, expect, vi } from 'vitest';
import 'dotenv/config';

const { Session } = require('../src/pkjs/session.js');

function makeSession() {
    const s = new Session('prompt', null);
    s.sent = [];
    s.enqueue = function(msg) { this.sent.push(msg); };
    return s;
}

describe('end-of-reply marker', () => {
    it('completes immediately when the message ends with [done]', () => {
        const s = makeSession();
        const resolve = vi.fn();

        s.handleIncomingMessage('Here is your answer. [done]', resolve);

        expect(s.sent[0]).toEqual({ CHAT: 'Here is your answer.' });
        expect(s.sent[1]).toEqual({ CHAT_DONE: true });
        expect(resolve).toHaveBeenCalledWith({ complete: true });
        expect(s._doneTimer == null).toBe(true);
    });

    it('is case-insensitive and strips surrounding whitespace', () => {
        const s = makeSession();
        s.handleIncomingMessage('All set.  [DONE] ', vi.fn());
        expect(s.sent[0]).toEqual({ CHAT: 'All set.' });
        expect(s.sent[1]).toEqual({ CHAT_DONE: true });
    });

    it('a bare [done] message closes the conversation without an empty CHAT', () => {
        const s = makeSession();
        s.handleIncomingMessage('[done]', vi.fn());
        expect(s.sent).toEqual([{ CHAT_DONE: true }]);
    });

    it('falls back to the idle timer when there is no marker', () => {
        vi.useFakeTimers();
        try {
            const s = makeSession();
            const resolve = vi.fn();
            s.handleIncomingMessage('part one', resolve);

            expect(s.sent).toEqual([{ CHAT: 'part one' }]);
            expect(resolve).not.toHaveBeenCalled();

            vi.advanceTimersByTime(2100);
            expect(s.sent[1]).toEqual({ CHAT_DONE: true });
            expect(resolve).toHaveBeenCalledWith({ complete: true });
        } finally {
            vi.useRealTimers();
        }
    });

    it('does not treat [done] mid-message as a marker', () => {
        vi.useFakeTimers();
        try {
            const s = makeSession();
            s.handleIncomingMessage('the [done] flag is mid-sentence here', vi.fn());
            expect(s.sent).toEqual([{ CHAT: 'the [done] flag is mid-sentence here' }]);
        } finally {
            vi.useRealTimers();
        }
    });
});
