import { defineConfig } from '@playwright/test';
export default defineConfig({testDir:'test',testMatch:'*.spec.ts',use:{baseURL:'http://127.0.0.1:5173',headless:true,locale:'ru-RU'},webServer:{command:'npm run dev',url:'http://127.0.0.1:5173',reuseExistingServer:false}});
