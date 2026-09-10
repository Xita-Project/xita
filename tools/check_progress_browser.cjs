// PLAYWRIGHT_MODULE=/path/to/playwright node tools/check_progress_browser.cjs URL OUTPUT_DIR
const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const url=process.argv[2],out=process.argv[3];
(async()=>{
  const browser=await chromium.launch({headless:true});
  try {
    const page=await browser.newPage();const errors=[];
    page.on('pageerror',e=>errors.push(e.message));
    if(url.startsWith('http')) await page.route('**/*',async route=>{
      const response=await route.fetch();
      await route.fulfill({response,headers:{...response.headers(),'Content-Security-Policy':"default-src 'self'; img-src 'self'; style-src 'self'; script-src 'self'; object-src 'none'; base-uri 'self'"}});
    });
    await page.goto(url);await page.waitForFunction(()=>document.querySelector('#visible').textContent.includes('entries shown'));
    assert.equal(await page.locator('h1').count(),1);
    const original=await page.locator('#visible').textContent();assert(original.includes('8,313'));
    for(const width of [1440,1024,768,390,320]) {
      await page.setViewportSize({width,height:1000});
      await page.waitForTimeout(100);
      assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),`overflow at ${width}`);
      if(out&&[1440,390].includes(width)){fs.mkdirSync(out,{recursive:true});await page.screenshot({path:path.join(out,`progress-${width}.png`),fullPage:true});}
    }
    await page.setViewportSize({width:1440,height:1000});
    await page.locator('#filter').selectOption('native');assert((await page.locator('#visible').textContent()).startsWith('5 /'));
    await page.locator('#map').click({position:{x:20,y:20}});assert(await page.locator('#detail dl').isVisible());
    await page.locator('.entry-list summary').click();
    await page.locator('#rows button').filter({hasText:'f_0005C300'}).click();
    assert((await page.locator('#detail').textContent()).includes('xv_math_bounds'));
    assert((await page.locator('#detail').textContent()).includes('Not recorded'));
    await page.getByRole('button',{name:'Explore source chunk'}).click();assert((await page.locator('#search').inputValue()).startsWith('code_'));
    await page.locator('#reset').click();assert.equal(await page.locator('#visible').textContent(),original);
    await page.locator('#search').fill('definitely-no-such-function');assert(await page.locator('#empty').isVisible());
    assert.equal(await page.locator('#rows button').count(),0);
    await page.locator('#reset').click();await page.locator('#filter').selectOption('unsupported');assert((await page.locator('#visible').textContent()).startsWith('44 /'));
    await page.locator('#next').click();assert((await page.locator('#page-number').textContent()).includes('2 of 2'));
    await page.locator('#previous').click();
    await page.locator('#filter').selectOption('boundary');assert((await page.locator('#visible').textContent()).startsWith('292 /'));
    await page.locator('#weight').selectOption('count');await page.locator('#rows button').first().focus();await page.keyboard.press('Enter');assert(await page.locator('#detail dl').isVisible());
    assert.deepEqual(errors,[]);
    console.log('PASS: profile report, responsive widths 320–1440, strict CSP, filters, search/empty state, canvas selection, chunk drilldown, pagination and keyboard entry selection');
  } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1});
