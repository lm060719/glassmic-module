// Playwright CLI probe after a mocked WebUI has a playing selection.
async (page) => {
  const position=()=>page.locator('#seek').inputValue().then(Number);
  const before=await position();
  await page.waitForFunction(previous=>Number(document.getElementById('seek').value)>previous+200,before);
  await page.evaluate(() => {
    const original=window.ksu.exec;
    window.__gmFailStatus=true;
    window.ksu.exec=function(command,options,callback) {
      if(window.__gmFailStatus&&command.endsWith(" 'status'")) {
        setTimeout(()=>window[callback](1,JSON.stringify({ok:false,error:'模拟服务断线'}),''),5);
      } else original(command,options,callback);
    };
  });
  await page.getByText('服务未连接',{exact:true}).waitFor();
  const disconnected=await position();
  await page.waitForTimeout(350);
  if(await position()!==disconnected)throw new Error('Disconnected progress continued advancing');
  if(!await page.locator('#play').isDisabled())throw new Error('Disconnected play was enabled');
  await page.evaluate(()=>window.__gmFailStatus=false);
  await page.getByText('服务已连接',{exact:true}).waitFor();
  if(await page.locator('#play').isDisabled())throw new Error('Recovered play was disabled');
  return {progressAdvanced:true,disconnectedPositionFrozen:true,controlsRecovered:true};
}
