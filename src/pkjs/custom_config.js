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

module.exports = function(minified) {
    var clayConfig = this;

    var telegramStatusText, botInput;
    var disconnectBtn;

    var SESSION_KEY = 'telegram_session';
    var BOT_USERNAME_KEY = 'agent_telegram_username';

    function setStatus(text, isError) {
        if (telegramStatusText) {
            telegramStatusText.set(text);
            if (isError) {
                telegramStatusText.$element[0].style.color = 'red';
            } else {
                telegramStatusText.$element[0].style.color = '';
            }
        }
    }

    function loadSession() {
        try { return localStorage.getItem(SESSION_KEY); } catch (e) { return null; }
    }

    function clearSession() {
        try { localStorage.removeItem(SESSION_KEY); } catch (e) {}
    }

    function getBotUsername() {
        var username = localStorage.getItem(BOT_USERNAME_KEY);
        try {
            var settings = JSON.parse(localStorage.getItem('clay-settings')) || {};
            if (!username) { username = settings.AGENT_TELEGRAM_USERNAME || '@MyAgentBot'; }
        } catch (e) {
            if (!username) { username = '@MyAgentBot'; }
        }
        if (username && !username.startsWith('@')) { username = '@' + username; }
        return username || '@MyAgentBot';
    }

    function saveBotUsername(username) {
        try {
            if (username && !username.startsWith('@')) { username = '@' + username; }
            localStorage.setItem(BOT_USERNAME_KEY, username);
        } catch (e) {}
    }

    function updateUI() {
        var session = loadSession();
        if (session) {
            setStatus('Connected (' + getBotUsername() + ')');
            if (disconnectBtn) disconnectBtn.show();
        } else {
            setStatus('Not connected');
            if (disconnectBtn) disconnectBtn.hide();
        }
    }

    clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function() {
        telegramStatusText = clayConfig.getItemById('telegramStatus');
        botInput = clayConfig.getItemByMessageKey('AGENT_TELEGRAM_USERNAME');
        disconnectBtn = clayConfig.getItemByMessageKey('TELEGRAM_DISCONNECT');

        updateUI();

        if (disconnectBtn) {
            disconnectBtn.on('click', function() {
                console.log('[config] Disconnect button clicked');
                clearSession();
                updateUI();
            });
        }

        if (botInput) {
            botInput.on('change', function() {
                var username = botInput.get();
                if (username) { saveBotUsername(username); }
            });
        }
    });
};