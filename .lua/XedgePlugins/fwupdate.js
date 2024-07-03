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

const respForm=[
  {
    el:"h2",
    html:"Upload successful!"
  },
  {
    el:"p",
    html:"Would you like to keep the application in deployed mode? If you intend to change the uploaded code, keep it in developer-mode."
  },
  {
    el:"fieldset",
    html:"",
    children:[
      {
	el: "legend",
	html: "Keep deployed"
      },
      {
	el:"input",
	type: "radio",
	label:"depTrue",
	rname:"Use ZIP file (deployed-mode)",
	name:"DeplCfg",
	value:"true",
	checked:"true",
      },
      {el:"br"},
      {	   
	el:"input",
	type: "radio",
	label:"depFalse",
	rname:"Unpack ZIP file (developer-mode)",
	name:"DeplCfg",
	value:"false",
      },
      {
	id: "DeplCfgSave",
	el: "input",
	type: "button",
	value: "Save"
      }
    ]
  }
];

ideCfgCB.push(function (mlist) {
  mlist.append($('<li>').text("Firmware Update ＆ App Upload").on("click", () => {
    sendCmd("getfwver", (rsp) => {
      if (!rsp.projectname) logR(rsp.err);
      else {
	logR('\nDrag and drop an <a target="_blank" href="https://realtimelogic.com/articles/Mastering-Xedge-Application-Deployment-From-Installation-to-Creation">application (zip file)</a> or\nDrag and drop a <a target="_blank" href="https://realtimelogic.com/ba/ESP32/source/GettingStarted.html#ota-upgrade">valid firmware file</a> to upload, flash, and restart device!\n');
	log("Current Firmware\nName: \t\t" + rsp.projectname + "\nVersion: \t" + rsp.version + "\nTime: \t\t" + rsp.time + "\nDate: \t\t" + rsp.date + "\nIDF: \t\t" + rsp.idfver + "\n");
	$('head').append(`
<style>
  #FMWUpdate {width:100%;height:100%;background-color:rgba(0,0,0,0.5);padding:20px;}
  #FMWUpdate>div {aspect-ratio: 1/1;position: relative;width: auto;height: 100%;margin: 0 auto;} 
  #fwguage>svg text {font-size:60%;font-family:Arial;}
  #fwdrop input {position: absolute;z-index:2;top:0;bottom:0;right:0;left:0;opacity:0;cursor:pointer;}
  #fwdrop>svg * {fill: #F8F8F8;}
  #fwdrop polygon, #fwdrop rect {transform-origin: center; transition: transform 0.2s ease-in-out;}
  #fwdrop:hover svg polygon, #FMWUpdate.dragover #fwdrop svg polygon, #fwdrop:hover svg rect, #FMWUpdate.dragover #fwdrop svg rect {transform:	scaleY(0.8) scaleX(1.1); fill: #FFF;}
  

</style>`)
	createFMWUpdate();
      }
    });
  }));
});

function createFMWUpdate() {
  let elems = {};
  const topElem = $('<div id="FMWUpdate">')
  let pe = mkForm(fwObj, elems, topElem, true);
  let editorId = createEditor("Firmware Update ＆ App Upload", null, null, pe);
  const setFwGuage = (val, endv ) => {
    elems.fwguage.find("path").attr("stroke-dasharray", `${val}, 100`);
    elems.fwguage.find("text").text(val + (endv || "%")).attr('fill', endv ? "#2196F3" : "#F8F8F8");
  };
  const sendFile = (file) => {
    let fn=file.name;
    const regex = /\.(bin|zip)$/;
    if (!regex.test(fn)) {
	alertErr('Invalid file type. Please upload a firmware file with a .bin or an app with .zip extension.');
	return;
    }
    let isBin=fn.endsWith('.bin') ? true : false;
    let expStatus=isBin ? 204 : 200;
    let xhr = new XMLHttpRequest();
    xhr.onreadystatechange = function () {
      if (xhr.readyState == 4) {
	if (expStatus != xhr.status) {
	  let err = xhr.getResponseHeader("X-Error");
	  if (!err) err = xhr.responseText;
	  pe.html(`<h2>Update Failed</h2><p>${err}</p>`);
	}
	else {
	  setFwGuage(100);
	  if(isBin)
	  {
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
	  else {
	    const rsp = JSON.parse(xhr.responseText);
	    if(rsp.ok) {
	      const form=$('<div class="form">');
	      mkForm(respForm,elems,form);
	      topElem.empty().append(form);
	      elems.DeplCfgSave.click(function() {
		let deploy = $('input[name="DeplCfg"]:checked').val();
		console.log(deploy);
		fetch('assets/loader.html').then(response => response.text()).then(html => {
		  topElem.html(html);
		  sendCmd("startApp",(rsp,x)=>{
		    if(false == rsp) rsp=x;
		    console.log(rsp);
		    topElem.html(`<h2>${rsp.ok ? (rsp.upgrade ? "App Upgraded" : "App Installed") : "Installing App Failed"}</h2><p>${rsp.ok ? rsp.info : rsp.err}</p>`);
		    createTree();
		  },{
		    name:fn,
		    upgrade:rsp.upgrade,
		    deploy:deploy,
		  });
		});
	      });
	    }
	    else
	      topElem.html(`<h2>Upload Failed</h2>${rsp.err}`);
	  }
	}
      }
    };
    xhr.upload.addEventListener("progress", function (e) {
      let x = Math.round(e.loaded * 100 / file.size)
      if (e.lengthComputable) setFwGuage(x < 50 ? x : (x - 5));
    }, false);
    xhr.upload.addEventListener("error", function (e) {
      setTimeout(function () {
	alertErr("Uploading " + fn + " failed!");
	closeEditor(editorId);
      }, 100);
    }, false);
    xhr.upload.addEventListener("abort", () => closeEditor(editorId), false);
    xhr.open("PUT", "private/command.lsp?cmd=uploadfw");
    xhr.setRequestHeader("X-Requested-With", "upload");
    xhr.setRequestHeader("X-File-Name", fn);
    xhr.send(file);
    elems.fwdrop.hide();
    elems.fwguage.show();
  }

  elems.fwguage.hide();
  pe.bind('dragover', (e) =>  {	 if(!topElem.hasClass('dragover')) topElem.addClass('dragover'); });
  pe.bind('dragleave', (e) =>  {  topElem.removeClass('dragover'); });
  topElem.find('input').bind('change', (e) => { const file = e.target.files[0]; if (file) sendFile(file); });
  topElem.unbind('drop').bind('drop', (e) => {
    topElem.removeClass('dragover');
    e.preventDefault();
    var file = e.originalEvent.dataTransfer.files[0];
    sendFile(file);
  });
  setFwGuage(90);
}
