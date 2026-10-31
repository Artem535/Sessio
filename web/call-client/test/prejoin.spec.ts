import { expect, test } from '@playwright/test';
test('requires a name and clears invitation fragment', async ({page}) => {
  const errors:string[]=[];page.on('pageerror',error=>errors.push(error.message));
  await page.goto('/join#code=test-code&passcode=test-pass');
  await expect(page.getByRole('button',{name:'Подключиться',exact:true})).toBeDisabled();
  await page.getByLabel('Имя',{exact:true}).fill('Гость');
  await expect(page.getByRole('button',{name:'Подключиться',exact:true})).toBeEnabled();
  await expect(page).toHaveURL('http://127.0.0.1:5173/join');
  await page.screenshot({path:'/tmp/sessio-125-prejoin.png'});
  expect(errors).toEqual([]);
});
test('invalid link renders safely', async ({page}) => {
  await page.goto('/join');
  await expect(page.getByText('Приглашение недействительно')).toBeVisible();
});
test('permission denial permits muted join and sends web contract once', async ({page}) => {
  await page.addInitScript(()=>{Object.defineProperty(navigator,'mediaDevices',{value:{getUserMedia:()=>Promise.reject(new DOMException('denied','NotAllowedError')),enumerateDevices:()=>Promise.resolve([])}});});
  let requests=0;
  await page.route('**/v1/invitations/**/client-token',route=>{requests++; const body=route.request().postDataJSON(); expect(body.clientKind).toBe('web');expect(body.displayName).toBe('Гость');return route.fulfill({status:410,contentType:'application/json',body:'{"error":"meeting_window_closed"}'});});
  await page.goto('/join#code=test-code&passcode=test-pass');
  await page.getByRole('button',{name:'Проверить устройства',exact:true}).click();
  await expect(page.getByText('Устройства недоступны. Можно войти без камеры и микрофона.')).toBeVisible();
  await page.getByLabel('Имя',{exact:true}).fill('Гость');
  await page.getByRole('button',{name:'Подключиться',exact:true}).click();
  await expect(page.getByRole('alert')).toBeVisible();expect(requests).toBe(1);
});
