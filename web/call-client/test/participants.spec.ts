import { expect, test } from '@playwright/test';

test('participants fill the stage, self stays beside them on desktop and mobile, fullscreen exits',async({page})=>{
  const errors:string[]=[];page.on('pageerror',error=>errors.push(error.message));
  await page.goto('/');
  await page.evaluate(async()=>{
    const reactPath='/node_modules/.vite/deps/react.js';
    const domPath='/node_modules/.vite/deps/react-dom_client.js';
    const callPath='/src/CallRoom.tsx';
    const React=(await import(reactPath)).default, {createRoot}=(await import(domPath)).default, {CallRoom}=await import(callPath);
    const participant=(identity:string,isLocal=false)=>({identity,name:identity,isLocal,getTrackPublication:()=>undefined});
    const room={localParticipant:participant('Вы',true),remoteParticipants:new Map([['other',participant('Собеседник')]]),on:()=>{},off:()=>{}};
    const main=document.querySelector('main')!;main.className='in-call';main.replaceChildren();
    createRoot(main).render(React.createElement(CallRoom,{room,onLeave:()=>{}}));
  });
  await expect(page.getByRole('button',{name:'Авто',exact:true})).toHaveAttribute('aria-pressed','true');
  await expect(page.locator('.pip-preview')).toBeVisible();
  for(const mode of ['Авто','Сетка','Поверх','Сбоку']){
    await page.getByRole('button',{name:mode,exact:true}).click();
    await expect(page.getByRole('button',{name:mode,exact:true})).toHaveAttribute('aria-pressed','true');
    await page.setViewportSize({width:1280,height:800});
    if(mode==='Авто' || mode==='Поверх'){
      const main=await page.locator('.participants-main').boundingBox(),self=await page.locator('.pip-preview').boundingBox(),controls=await page.locator('.controls').boundingBox();
      expect(self!.x).toBeGreaterThan(main!.x+main!.width/2);
      expect(self!.x+self!.width).toBeLessThanOrEqual(main!.x+main!.width);
      expect(self!.y+self!.height).toBeLessThan(controls!.y);
    } else if(mode==='Сетка') {
      await expect(page.locator('.grid .tile')).toHaveCount(2);
      await expect(page.locator('.self-preview')).toHaveCount(0);
    }
    await page.screenshot({path:`/tmp/sessio-layout-${mode}.png`});
  }
  await expect(page.locator('.participants-main .tile')).toHaveCount(1);
  await expect(page.locator('.self-preview .tile')).toHaveCount(1);
  for(const width of [1280,375]){
    await page.setViewportSize({width,height:800});
    const main=await page.locator('.participants-main').boundingBox(),self=await page.locator('.self-preview').boundingBox();
    expect(main!.width).toBeGreaterThan(self!.width);
    expect(self!.x).toBeGreaterThanOrEqual(main!.x+main!.width);
    expect(self!.x+self!.width).toBeLessThanOrEqual(width);
    await page.screenshot({path:`/tmp/sessio-147-${width}.png`});
  }
  await page.getByRole('button',{name:'На весь экран',exact:true}).click();
  await expect(page.getByRole('button',{name:'Выйти из полного экрана'})).toBeVisible();
  await page.evaluate(()=>document.exitFullscreen());
  await expect(page.getByRole('button',{name:'На весь экран',exact:true})).toBeVisible();
  await page.getByRole('button',{name:'Сетка',exact:true}).click();
  await expect(page.locator('.grid .tile')).toHaveCount(2);
  expect(errors).toEqual([]);
});
