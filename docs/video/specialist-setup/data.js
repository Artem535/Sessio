// Voice-over rows from the scenario: [start s, end s, text]. Shared by the
// page (burned-in subtitles) and make-srt.mjs.
(function (root) {
  const ROWS = [
    [0, 15, "Сейчас настроим звонки в Sessio, создадим еженедельную встречу и получим одну ссылку, которой клиент сможет пользоваться для всех повторений."],
    [15, 50, "Откройте настройки LiveKit. Укажите имя, которое увидит клиент. Адрес сервиса и ваш ключ доступа выдаёт администратор. Вставьте адрес в поле URL токен-бэкенда, а ключ — в поле Bearer-токен."],
    [50, 65, "Нажмите «Сохранить». Ключ хранится в системном хранилище. Для следующих встреч вводить его заново не нужно."],
    [65, 105, "Создайте встречу и задайте дату, время и длительность. Включите онлайн-сессию и выберите LiveKit. Для регулярных консультаций задайте еженедельное повторение. Сохраните встречу."],
    [105, 125, "Дождитесь подтверждения, что расписание синхронизировано, а постоянная ссылка готова. Так мы знаем, что сервер получил даты встреч и может принимать клиента по приглашению."],
    [125, 150, "Скопируйте приглашение и передайте его клиенту привычным способом. Это приглашение относится ко всей серии: каждую неделю отправлять новую ссылку не потребуется."],
    [150, 190, "Перед началом откройте нужную встречу. Проверьте изображение камеры и выберите микрофон и устройство вывода звука. Наушники помогут избежать эха. Пока вы не присоединились, предпросмотр видите только вы."],
    [190, 210, "Нажмите «Присоединиться». Клиент входит по своему приглашению. Теперь вы в одной комнате и можете начать консультацию."],
    [210, 235, "Для следующей встречи используется та же ссылка. Если вы переносите или отменяете встречу, дождитесь синхронизации изменения. Пока приложение ожидает сеть, на сервере может действовать прежнее расписание."],
    [235, 245, "Настройка завершена. Перед первым звонком стоит проверить подключение вместе с клиентом."],
  ];

  // Split each row into sentence-sized cues, timed by character count.
  function cues() {
    const out = [];
    for (const [t0, t1, text] of ROWS) {
      const parts = text.match(/[^.!?]+[.!?]+»?\s*|[^.!?]+$/g).map((s) => s.trim());
      const total = parts.reduce((a, p) => a + p.length, 0);
      // Break cues longer than ~95 chars at the comma/dash nearest the middle.
      const split = [];
      for (const p of parts) {
        if (p.length <= 95) { split.push(p); continue; }
        const mid = p.length / 2;
        let best = -1;
        for (const m of p.matchAll(/[,—:] ?/g)) { const i = m.index + m[0].length; if (best < 0 || Math.abs(i - mid) < Math.abs(best - mid)) best = i; }
        if (best > 20 && best < p.length - 20) split.push(p.slice(0, best).trim(), p.slice(best).trim()); else split.push(p);
      }
      const total2 = split.reduce((a, p) => a + p.length, 0);
      let t = t0;
      for (const p of split) {
        const d = ((t1 - t0) * p.length) / total2;
        out.push({ t0: t, t1: t + d, text: p });
        t += d;
      }
    }
    return out;
  }

  const api = { ROWS, cues, DURATION: 245 };
  if (typeof module !== "undefined") module.exports = api;
  else root.SCRIPT_DATA = api;
})(typeof window !== "undefined" ? window : globalThis);
