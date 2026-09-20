ideCfgCB.push(m=>{
  let b=[...m.children].find(e=>e.textContent==="App Upload");
  if(b) b.textContent="Firmware Update & App Upload";
  else m.append(b=el("li",{text:"Firmware Update & App Upload"}));
  b.onclick=()=>sendCmd("getfwver",r=>{
    if(!r) return;
    if(r.err||!r.projectname) return alert(r.err||"No firmware information.");
    logR('\nDrag and drop an <a target="_blank" href="https://realtimelogic.com/articles/Mastering-Xedge-Application-Deployment-From-Installation-to-Creation">application (zip file)</a> or\nDrag and drop a <a target="_blank" href="https://realtimelogic.com/ba/ESP32/source/GettingStarted.html#ota-upgrade">valid firmware file</a> to upload, flash, and restart device!\n');
    log(`Current Firmware\nName:\t\t${r.projectname}\nVersion:\t${r.version}\nTime:\t\t${r.time}\nDate:\t\t${r.date}\nIDF:\t\t${r.idfver}\n`);
    uploadEditor({firmware:true,title:"Firmware Update & App Upload"});
  });
});
