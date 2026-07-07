import { describe, it, expect, beforeEach, vi } from 'vitest';
import 'dotenv/config';

describe('MessageQueue chunking', () => {
    let sendAppMessage;
    let Queue;

    beforeEach(() => {
        sendAppMessage = vi.fn((msg, success, failure) => {
            if (success) success();
        });
        global.Pebble = { sendAppMessage };

        // Re-require with a fresh module cache so the queue starts empty.
        var path = require.resolve('../src/pkjs/lib/message_queue.js');
        delete require.cache[path];
        Queue = require('../src/pkjs/lib/message_queue.js').Queue;
    });

    it('sends a small CHAT in one message', () => {
        Queue.enqueue({ CHAT: 'hello' });
        expect(sendAppMessage).toHaveBeenCalledTimes(1);
        expect(sendAppMessage.mock.calls[0][0].CHAT).toBe('hello');
    });

    it('chunks a large CHAT into multiple messages', () => {
        // Build a payload well over MAX_CHAT_CHUNK_SIZE (4000).
        var big = '';
        for (var i = 0; i < 10000; i++) big += 'x';
        Queue.enqueue({ CHAT: big });

        expect(sendAppMessage.mock.calls.length).toBeGreaterThan(1);
        var reassembled = '';
        for (var c = 0; c < sendAppMessage.mock.calls.length; c++) {
            reassembled += sendAppMessage.mock.calls[c][0].CHAT;
        }
        expect(reassembled).toBe(big);
    });

    it('chunks by UTF-8 byte length so multibyte text fits the watch inbox', () => {
        // 3000 characters that each encode to 3 UTF-8 bytes (9000 bytes total):
        // under the old code-unit limit of 4000 this went out as a single
        // message and blew the watch's 5000-byte inbox.
        var big = '';
        for (var i = 0; i < 3000; i++) big += '日';
        Queue.enqueue({ CHAT: big });

        expect(sendAppMessage.mock.calls.length).toBeGreaterThan(1);
        var reassembled = '';
        for (var c = 0; c < sendAppMessage.mock.calls.length; c++) {
            var chunk = sendAppMessage.mock.calls[c][0].CHAT;
            expect(Buffer.byteLength(chunk, 'utf8')).toBeLessThanOrEqual(4000);
            reassembled += chunk;
        }
        expect(reassembled).toBe(big);
    });

    it('never splits a surrogate pair across chunks', () => {
        // Emoji are surrogate pairs (4 UTF-8 bytes each).
        var big = '';
        for (var i = 0; i < 1500; i++) big += '😀';
        Queue.enqueue({ CHAT: big });

        var reassembled = '';
        for (var c = 0; c < sendAppMessage.mock.calls.length; c++) {
            var chunk = sendAppMessage.mock.calls[c][0].CHAT;
            // A well-formed chunk never starts with a trailing surrogate or
            // ends with a leading surrogate.
            expect(chunk.charCodeAt(0)).not.toBeGreaterThanOrEqual(0xDC00);
            expect(chunk.charCodeAt(chunk.length - 1)).not.toBeLessThan(0xDC00);
            reassembled += chunk;
        }
        expect(reassembled).toBe(big);
    });

    it('retries a failed send and delivers on a later attempt', async () => {
        var attempts = 0;
        sendAppMessage.mockImplementation((msg, success, failure) => {
            attempts++;
            if (attempts < 3) {
                failure();
            } else {
                success();
            }
        });

        Queue.enqueue({ CHAT_DONE: true });
        // Retries are scheduled at 250ms and then 500ms.
        await new Promise(r => setTimeout(r, 900));

        expect(attempts).toBe(3);
    });

    it('gives up after the retry budget is exhausted and keeps pumping', async () => {
        sendAppMessage.mockImplementation((msg, success, failure) => {
            if (msg.CHAT === 'doomed') {
                failure();
            } else {
                success();
            }
        });

        Queue.enqueue({ CHAT: 'doomed' });
        Queue.enqueue({ CHAT: 'next' });
        await new Promise(r => setTimeout(r, 900));

        var sent = sendAppMessage.mock.calls.map(c => c[0]);
        expect(sent.filter(m => m.CHAT === 'doomed').length).toBe(3);
        expect(sent.some(m => m.CHAT === 'next')).toBe(true);
    });

    it('preserves order of surrounding messages around a chunked CHAT', () => {
        var big = '';
        for (var i = 0; i < 5000; i++) big += 'y';
        Queue.enqueue({ CHAT: big });
        Queue.enqueue({ CHAT_DONE: true });

        var keys = sendAppMessage.mock.calls.map(function(c) { return Object.keys(c[0])[0]; });
        var lastKey = keys[keys.length - 1];
        expect(lastKey).toBe('CHAT_DONE');
    });
});