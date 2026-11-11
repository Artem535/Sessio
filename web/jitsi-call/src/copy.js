const strings = {
  en: {
    connecting: 'Connecting…',
    lost: 'Connection lost, the call has ended',
    kicked: 'You were removed from the call',
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
    lost: 'Соединение потеряно, звонок завершён',
    kicked: 'Вас удалили из звонка',
    left: 'Звонок завершён',
    failed: 'Не удалось войти в звонок',
    mic: 'Микрофон',
    camera: 'Камера',
    leave: 'Выйти',
    you: 'Вы',
    waiting: 'Ждём второго участника…',
  },
};

let currentLanguage = typeof navigator !== 'undefined' ? navigator.language : 'en';

export function pickLocale(language) {
  return /^ru\b/i.test(language || '') ? 'ru' : 'en';
}

export function setLocale(language) {
  if (language) currentLanguage = language;
}

export function locale() {
  return pickLocale(currentLanguage);
}

export function copy(key) {
  return strings[locale()][key] ?? strings.en[key] ?? key;
}
