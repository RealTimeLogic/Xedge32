const fwObj = [
  {
    el: "div", id: "fwguage",
    html: '<svg width="100%" height="100%" viewBox="0 0 36 36"><path d="M18 2.0845 a 15.9155 15.9155 0 0 1 0 31.831 a 15.9155 15.9155 0 0 1 0 -31.831" stroke-linecap="round" fill="none" stroke="#8F8F8F"; stroke-width="1"; stroke-dasharray="0, 100"/><text x="18" y="18" text-anchor="middle" alignment-baseline="middle"></text></svg>'
  },
  {
    el: "div", id: "fwdrop",
    html: '<label><input type="file" /><svg width="100%" height="100%" viewBox="0 0 36 36"><circle cx="18" cy="18" r="18" fill="#1f1f1f" /><polygon fill="#8f8f8f" points="13,15 23,15 18,10"/><rect class="fwarrowstem" fill="#8f8f8f" x="16" y="15" width="4" height="5"/><rect x="13" y="22" width="10" height="2" fill="#8f8f8f" /></svg></label>'
  }
];

ideCfgCB.push(function (mlist) {
  mlist.append($('<li>').text("Firmware Update").on("click", () => {
    sendCmd("getfwver", (rsp) => {
      if (!rsp.projectname) logR(rsp.err);
      else {
        logR('\nDrag and drop a valid firmware file to upload, flash, and restart device!\n');
        log("Current Firmware\nName: \t\t" + rsp.projectname + "\nVersion: \t" + rsp.version + "\nTime: \t\t" + rsp.time + "\nDate: \t\t" + rsp.date + "\nIDF: \t\t" + rsp.idfver + "\n");
        $('head').append(`
<style>
  #FMWUpdate {width:100%;height:100%;background-color:rgba(0,0,0,0.5);padding:20px;}
  #FMWUpdate>div {aspect-ratio: 1/1;position: relative;width: auto;height: 100%;margin: 0 auto;} 
  #fwguage>svg text {font-size:60%;font-family:Arial;}
  #fwdrop input {position: absolute;z-index:2;top:0;bottom:0;right:0;left:0;opacity:0;cursor:pointer;}
  #fwdrop>svg * {fill: #F8F8F8;}
  #fwdrop polygon, #fwdrop rect {transform-origin: center; transition: transform 0.2s ease-in-out;}
  #fwdrop:hover svg polygon, #FMWUpdate.dragover #fwdrop svg polygon, #fwdrop:hover svg rect, #FMWUpdate.dragover #fwdrop svg rect {transform:  scaleY(0.8) scaleX(1.1); fill: #FFF;}
  

</style>`)
        createFMWUpdate();
      }
    });
  }));
});

function createFMWUpdate() {
  let elems = {};
  const $form = $('<div id="FMWUpdate">')
  let pe = mkForm(fwObj, elems, $form, true);
  let editorId = createEditor(" Firmware Update", null, null, pe);

  const setFwGuage = (val, endv ) => {
    elems.fwguage.find("path").attr("stroke-dasharray", `${val}, 100`);
    elems.fwguage.find("text").text(val + (endv || "%")).attr('fill', endv ? "#2196F3" : "#F8F8F8");
  };
  const sendFile = (file) => {  
    const regex = /\.bin$/;
    if (!regex.test(file.name)) {
      alertErr('Invalid file type. Please upload a firmware file with a .bin extension.');
      return;
    }

    let xhr = new XMLHttpRequest();
    xhr.onreadystatechange = function () {
      if (xhr.readyState == 4) {
        if (204 != xhr.status) {
          let err = xhr.getResponseHeader("X-Error");
          if (!err) err = xhr.responseText;
          pe.html(`<h2>Update Failed</h2><p>${err}</p>`);
        }
        else {
          setFwGuage(100);
          setTimeout(() => {
            setFwGuage("RESE", "T");
            log("Device is restarting\n");
            setInterval(() => {
              $.ajax({
                url: '/', method: 'GET', timeout: 1900,
                success: () => location.reload(true),
                error: (xhr) => {
                  if (xhr.status === 404) location.reload(true);
                }
              });
              log(".");
            }, 2000);
          }, 5000);
        }
      }
    };
    xhr.upload.addEventListener("progress", function (e) {
      let x = Math.round(e.loaded * 100 / file.size)
      if (e.lengthComputable) setFwGuage(x < 50 ? x : (x - 5));
    }, false);
    xhr.upload.addEventListener("error", function (e) {
      setTimeout(function () {
        alertErr("Uploading " + file.name + " failed!");
        closeEditor(editorId);
      }, 100);
    }, false);
    xhr.upload.addEventListener("abort", () => closeEditor(editorId), false);
    xhr.open("PUT", "private/command.lsp?cmd=uploadfw");
    xhr.setRequestHeader("x-requested-with", "upload")
    xhr.send(file);
    elems.fwdrop.hide();
    elems.fwguage.show();
  }

  elems.fwguage.hide();
  pe.bind('dragover', (e) =>  {  if(!$form.hasClass('dragover')) $form.addClass('dragover'); });
  pe.bind('dragleave', (e) =>  {  $form.removeClass('dragover'); });
  $form.find('input').bind('change', (e) => { const file = e.target.files[0]; if (file) sendFile(file); });
  $form.unbind('drop').bind('drop', (e) => {
    $form.removeClass('dragover');
    e.preventDefault();
    var file = e.originalEvent.dataTransfer.files[0];
    sendFile(file);
  });
  setFwGuage(90);
}
