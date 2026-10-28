const strings = {
  en: {
    connecting: 'Connecting…',
    connected: 'Connected',
    reconnecting: 'Connection lost',
    left: 'Call ended',
    failed: 'Could not join the call',
    mic: 'Microphone',
    camera: 'Camera',
    leave: 'Leave',
    you: 'You',
    waiting: 'Waiting for the other participant…',
  },
  ru: {
    connecting: 'Подключение…',
    connected: 'Соединение установлено',
    reconnecting: 'Соединение потеряно',
    left: 'Звонок завершён',
    failed: 'Не удалось войти в звонок',
    mic: 'Микрофон',
    camera: 'Камера',
    leave: 'Выйти',
    you: 'Вы',
    waiting: 'Ждём второго участника…',
  },
};

export function pickLocale(language) {
  return /^ru\b/i.test(language || '') ? 'ru' : 'en';
}

export function copy(key, language = typeof navigator !== 'undefined' ? navigator.language : 'en') {
  return strings[pickLocale(language)][key] ?? strings.en[key] ?? key;
}
