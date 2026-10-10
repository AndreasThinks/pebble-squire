// Installs fake GramJS globals (TelegramClient, StringSession, TelegramApi)
// so the auth/client modules can be exercised offline.
const bundleLoader = require('../../src/pkjs/lib/bundle_loader.js');

const SESSION_SECRET = 'SESSION-SECRET-abcdef';

// vi is passed in because vitest can't be require()d from CommonJS.
function install(vi, options) {
    options = options || {};
    const state = {
        instances: [],
        invocations: [],
        passwordAttempts: 0,
        correctPassword: options.correctPassword || 'correct horse',
        onInvoke: options.onInvoke || (() => Promise.resolve({})),
    };

    function StringSession(value) { this.value = value || ''; }
    StringSession.prototype.save = function() { return this.value || SESSION_SECRET + '-' + state.instances.length; };
    StringSession.prototype.setDC = function() {};

    function TelegramClient(session, apiId, apiHash) {
        this.session = session;
        this.apiId = apiId;
        this.apiHash = apiHash;
        this.connected = false;
        this.destroyed = false;
        state.instances.push(this);
    }
    TelegramClient.prototype.connect = function() {
        if (options.connectError) return Promise.reject(options.connectError);
        this.connected = true;
        return Promise.resolve();
    };
    TelegramClient.prototype.isUserAuthorized = function() {
        return Promise.resolve(options.authorized !== false);
    };
    TelegramClient.prototype.destroy = function() {
        this.connected = false;
        this.destroyed = true;
        return Promise.resolve();
    };
    TelegramClient.prototype.invoke = function(request) {
        state.invocations.push(request);
        return state.onInvoke(request, this);
    };
    // Mirrors GramJS's signInWithPassword: it loops, asking for the password
    // again after every error, until onError returns true.
    TelegramClient.prototype.signInWithPassword = async function(creds, params) {
        for (let guard = 0; guard < 50; guard++) {
            try {
                const password = await params.password('hint');
                state.passwordAttempts++;
                if (password !== state.correctPassword) {
                    throw Object.assign(new Error('PASSWORD_HASH_INVALID'), { errorMessage: 'PASSWORD_HASH_INVALID' });
                }
                return { id: 1 };
            } catch (err) {
                if (await params.onError(err)) {
                    throw new Error('AUTH_USER_CANCEL');
                }
            }
        }
        throw new Error('signInWithPassword looped 50 times');
    };

    function request(className) {
        return function(params) { Object.assign(this, params || {}); this.className = className; };
    }
    const TelegramApi = {
        auth: {
            SendCode: request('auth.SendCode'),
            SignIn: request('auth.SignIn'),
            LogOut: request('auth.LogOut'),
        },
        CodeSettings: request('CodeSettings'),
    };

    global.TelegramClient = TelegramClient;
    global.StringSession = StringSession;
    global.TelegramApi = TelegramApi;
    vi.spyOn(bundleLoader, 'ensureTelegramBundle').mockImplementation(() => {});
    return state;
}

function uninstall() {
    delete global.TelegramClient;
    delete global.StringSession;
    delete global.TelegramApi;
}

function telegramError(code) {
    return Object.assign(new Error(code), { errorMessage: code });
}

module.exports = { install, uninstall, telegramError, SESSION_SECRET };
