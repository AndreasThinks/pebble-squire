/**
 * Copyright 2025 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * Storage for the Telegram session and the agent bot username.
 *
 * The session string is a bearer credential: it contains the MTProto auth
 * key, and anyone who obtains it can act as the user's Telegram account
 * until the session is revoked (Telegram > Settings > Devices). PebbleKit JS
 * offers no OS keystore, so it lives in the app's localStorage, which the
 * phone app keeps in its private storage. Encrypting it with a key shipped in
 * this same JavaScript would add no protection, so we don't pretend to.
 * Instead: only this module touches it, it is never logged, never sent to
 * the watch or the config page, and it is deleted on sign-out.
 */

var SESSION_KEY = 'telegram_session';
// Older versions cached the bot username here, where it silently overrode
// later changes made on the settings page. Only clay-settings is used now.
var LEGACY_BOT_USERNAME_KEY = 'agent_telegram_username';

// Telegram usernames: 5-32 characters, letters, digits and underscores,
// starting with a letter.
var USERNAME_RE = /^[A-Za-z][A-Za-z0-9_]{4,31}$/;

function saveSession(sessionData) {
    try {
        localStorage.setItem(SESSION_KEY, sessionData);
        console.log('[session] Telegram session saved');
        return true;
    } catch (err) {
        console.error('[session] Failed to save session: ' + (err && err.message));
        return false;
    }
}

function loadSession() {
    try {
        return localStorage.getItem(SESSION_KEY) || null;
    } catch (err) {
        console.error('[session] Failed to load session: ' + (err && err.message));
        return null;
    }
}

function clearSession() {
    try {
        localStorage.removeItem(SESSION_KEY);
        console.log('[session] Telegram session cleared');
    } catch (err) {
        console.error('[session] Failed to clear session: ' + (err && err.message));
    }
}

function hasSession() {
    return !!loadSession();
}

// Returns the canonical "@username" form, or '' if value isn't a valid
// Telegram username.
function normalizeBotUsername(value) {
    if (typeof value !== 'string') {
        return '';
    }
    var name = value.trim().replace(/^@/, '');
    return USERNAME_RE.test(name) ? '@' + name : '';
}

function readClaySettings() {
    try {
        return JSON.parse(localStorage.getItem('clay-settings')) || {};
    } catch (e) {
        console.error('[session] Failed to parse clay-settings');
        return {};
    }
}

/**
 * The configured agent bot, as "@username", or '' if none is configured (or
 * the configured value isn't a valid username). There is deliberately no
 * default: sending prompts and location to a guessed account would leak them.
 */
function getBotUsername() {
    return normalizeBotUsername(readClaySettings().AGENT_TELEGRAM_USERNAME);
}

function saveBotUsername(username) {
    var normalized = normalizeBotUsername(username);
    if (!normalized) {
        return false;
    }
    var settings = readClaySettings();
    settings.AGENT_TELEGRAM_USERNAME = normalized;
    localStorage.setItem('clay-settings', JSON.stringify(settings));
    return true;
}

function removeLegacyKeys() {
    try {
        localStorage.removeItem(LEGACY_BOT_USERNAME_KEY);
    } catch (e) {}
}

function getSessionInfo() {
    return {
        hasSession: hasSession(),
        botUsername: getBotUsername()
    };
}

exports.saveSession = saveSession;
exports.loadSession = loadSession;
exports.clearSession = clearSession;
exports.hasSession = hasSession;
exports.normalizeBotUsername = normalizeBotUsername;
exports.saveBotUsername = saveBotUsername;
exports.getBotUsername = getBotUsername;
exports.removeLegacyKeys = removeLegacyKeys;
exports.getSessionInfo = getSessionInfo;
