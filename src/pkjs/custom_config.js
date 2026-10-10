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

// Runs inside the configuration web page, not in PebbleKit JS: storage here
// is the page's own and isn't visible to the app, so nothing is saved here.
// It only tidies the bot username as it's typed.
module.exports = function(minified) {
    var clayConfig = this;

    clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function() {
        var botInput = clayConfig.getItemByMessageKey('AGENT_TELEGRAM_USERNAME');
        if (!botInput) {
            return;
        }
        botInput.on('change', function() {
            var username = (botInput.get() || '').trim();
            if (username && username.charAt(0) !== '@') {
                botInput.set('@' + username);
            }
        });
    });
};
