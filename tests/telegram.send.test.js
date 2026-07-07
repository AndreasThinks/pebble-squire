import { describe, it, expect } from 'vitest';
import 'dotenv/config';

// Live-API test: only runs when a bot username is configured in .env.
const botUsername = process.env.AGENT_TELEGRAM_USERNAME;

describe('Telegram send message', () => {
    it.skipIf(!botUsername)('should resolve bot username and send a message', async () => {

        const bundleLoader = require('../src/pkjs/lib/bundle_loader.js');
        bundleLoader.ensureTelegramBundle();

        const client = require('../src/pkjs/telegram/client.js');
        const session = require('../src/pkjs/telegram/session.js');

        const connected = await client.initClient();
        expect(connected).toBe(true);

        const tgClient = client.getClient();
        expect(tgClient).not.toBeNull();

        const cleanUsername = botUsername.replace(/^@/, '');
        console.log('Resolving bot: ' + cleanUsername);

        let entity;
        try {
            const result = await tgClient.invoke(new TelegramApi.contacts.ResolveUsername({ username: cleanUsername }));
            entity = result.users && result.users.length ? result.users[0] : (result.chats && result.chats.length ? result.chats[0] : null);
            console.log('ResolveUsername found:', entity ? 'id=' + entity.id + ' username=' + (entity.username || 'none') + ' bot=' + (entity.bot || false) : 'null');
        } catch (err) {
            console.log('ResolveUsername failed: ' + (err.errorMessage || err.message));
            console.log('Trying contacts.Search...');
            const searchResult = await tgClient.invoke(new TelegramApi.contacts.Search({ q: cleanUsername, limit: 5 }));
            console.log('Search returned ' + (searchResult.users ? searchResult.users.length : 0) + ' users');
            for (const u of (searchResult.users || [])) {
                if (u.username && u.username.toLowerCase() === cleanUsername.toLowerCase()) {
                    entity = u;
                    console.log('Found via Search: id=' + u.id + ' username=' + u.username);
                    break;
                }
            }
        }

        expect(entity).not.toBeNull();
        expect(entity).not.toBeUndefined();

        const result = await tgClient.sendMessage(entity, { message: 'test from squire unit test' });
        console.log('Message sent, id:', result ? result.id : 'unknown');
        expect(result).not.toBeNull();

        try { await client.disconnect(); } catch (e) {}
    }, 30000);
});