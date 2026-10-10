/**
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
 * Telegram API credentials (api_id / api_hash from my.telegram.org).
 *
 * These are injected at build time by wscript into the generated module
 * `squire_telegram_app` (build/js/squire_telegram_app.js); they are not read
 * from process.env, which doesn't exist in PebbleKit JS. Like every Telegram
 * client, the built app necessarily contains them in plain text.
 */

var generated = require('squire_telegram_app');

function isConfigured() {
    return !!(generated && generated.configured &&
              generated.apiId > 0 && /^[0-9a-f]{32}$/.test(generated.apiHash || ''));
}

// Returns {apiId, apiHash}, or throws an error with code APP_NOT_CONFIGURED.
function getCredentials() {
    if (!isConfigured()) {
        var err = new Error('This build of Squire has no Telegram API credentials (TELEGRAM_APP_ID / TELEGRAM_APP_HASH).');
        err.code = 'APP_NOT_CONFIGURED';
        throw err;
    }
    return { apiId: generated.apiId, apiHash: generated.apiHash };
}

exports.isConfigured = isConfigured;
exports.getCredentials = getCredentials;
