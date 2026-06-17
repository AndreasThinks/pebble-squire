var loaded = false;

function ensureTelegramBundle() {
    if (!loaded) {
        console.log('[bundle] Loading Telegram bundle...');
        try {
            var bundled = require('./telegram-bundle.js');
            if (bundled && bundled.TelegramClient) {
                (typeof window !== 'undefined' ? window : global).TelegramClient = bundled.TelegramClient;
            }
            if (bundled && bundled.StringSession) {
                (typeof window !== 'undefined' ? window : global).StringSession = bundled.StringSession;
            }
            if (bundled && bundled.Api) {
                (typeof window !== 'undefined' ? window : global).TelegramApi = bundled.Api;
            }
            if (bundled && bundled.TelegramClient && bundled.TelegramClient.events && bundled.TelegramClient.events.NewMessage) {
                (typeof window !== 'undefined' ? window : global).NewMessage = bundled.TelegramClient.events.NewMessage;
            }
            if (bundled && bundled.TelegramClient && bundled.TelegramClient.events && bundled.TelegramClient.events.Raw) {
                (typeof window !== 'undefined' ? window : global).Raw = bundled.TelegramClient.events.Raw;
            }
            loaded = true;
            console.log('[bundle] Telegram bundle loaded successfully');
            console.log('[bundle] TelegramClient available: ' + (typeof TelegramClient !== 'undefined'));
            console.log('[bundle] StringSession available: ' + (typeof StringSession !== 'undefined'));
            console.log('[bundle] TelegramApi available: ' + (typeof TelegramApi !== 'undefined'));
            console.log('[bundle] Raw available: ' + (typeof Raw !== 'undefined'));
        } catch (err) {
            console.error('[bundle] Failed to load Telegram bundle: ' + (err.message || err));
            console.error('[bundle] Stack: ' + (err.stack || 'no stack'));
        }
    }
}

exports.ensureTelegramBundle = ensureTelegramBundle;