const fwObj = [
    {
	el:"div",
	id:"fwguage",
	html:'<svg width="100%" height="100%" viewBox="0 0 36 36"><path d="M18 2.0845 a 15.9155 15.9155 0 0 1 0 31.831 a 15.9155 15.9155 0 0 1 0 -31.831" fill="none" stroke="#000"; stroke-width="1"; stroke-dasharray="0, 100"/><text x="18" y="20" font-family="Arial" font-size="10" text-anchor="middle" alignment-baseline="middle"></text></svg>'
    },
    {
	el:"div",
	id:"fwdrop",
	html:'<svg width="100%" height="100%" viewBox="0 0 36 36"><circle cx="18" cy="18" r="18" fill="#1f1f1f" /><polygon fill="#8f8f8f" /><rect class="fwarrowstem" fill="#8f8f8f" /><rect x="13" y="22" width="10" height="2" fill="#8f8f8f" /></svg>'
    }
];

ideCfgCB.push(function(mlist) {

    mlist.append($('<li>').text("Firmware Upgrade").on("click",()=>{
	logR('\nDrag and drop a valid firmware file to upload, flash, and restart device!\n');
	sendCmd("getfwver",(rsp)=>log("Current Firmware\n\tName: "+rsp.projectname+"\n\tVersion: "+rsp.version+"\n\tTime: "+rsp.time+"\n\tDate: "+rsp.date+"\n\tIDF: "+rsp.idfver+"\n"));
	let elems={};
	function setFwGuage(val,endv) {
	    var pe=elems.fwguage.find("path");
	    var da=pe.attr("stroke-dasharray").split(", ");
	    da[0]=val;
	    pe.attr("stroke-dasharray", da.join(", "));
	    elems.fwguage.find("text").text(val + (endv?endv:"%"));
	};
	function setDropArrow(hover) {
	    let el=elems.fwdrop;
	    if(hover) {
		el.find("polygon").attr("points", "13,18 23,18 18,13");
		el.find("rect.fwarrowstem").attr({"x":"15","y":"17","width":"6","height":"3"});
	    }
	    else {
		el.find("polygon").attr("points", "13,15 23,15 18,10");
		el.find("rect.fwarrowstem").attr({"x":"16","y":"15","width":"4","height":"5"});
	    }
	};
	let pe=mkForm(fwObj,elems,null,true);
	let editorId=createEditor(" Firmware Upgrade",null,null,pe);
	elems.fwguage.hide();
	setDropArrow();
	let timer;
	pe.bind('dragover',function(e) {
	    e.preventDefault();
	    if(timer)
		clearTimeout(timer);
	    else
		setDropArrow(true);
	    timer=setTimeout(()=>{timer=null; setDropArrow();},500);
	});
	$('body').unbind('drop').bind('drop',(e)=>{
	    e.preventDefault();
	    $('body').unbind('drop').bind('drop', function(e) {e.preventDefault();});
	    var file=e.originalEvent.dataTransfer.files[0];
	    const regex = /\.bin$/;
	    if( ! regex.test(file.name) ) {
		alertErr('Invalid file type. Please upload a firmware file with a .bin extension.');
		return;
	    }
	    let xhr = new XMLHttpRequest();
	    xhr.onreadystatechange=function(){
		if(xhr.readyState == 4) {
		    if(204 != xhr.status) {
			let err=xhr.getResponseHeader("X-Error");
			if(!err) err=xhr.responseText;
			pe.html("<h2>Upgrade Failed</h2><p>"+err+"</p>");
		    }
		    else {
			setFwGuage(100);
			setTimeout(()=>{
			    setFwGuage("RS","T");
			    log("Device is restarting\n");
			    setInterval(()=>{
				$.ajax({url:'/',method:'GET',timeout:1900,
					success:()=>location.reload(true),
					error:(xhr)=>{
					    if(xhr.status === 404) location.reload(true);
					}
				       });
				log(".");
			    }, 2000);
			},5000);
		    }
		}
	    };
	    xhr.upload.addEventListener("progress", function(e) {
		let x=Math.round(e.loaded * 100 / file.size)
		if(e.lengthComputable) setFwGuage(x < 50 ? x : (x-5));
	    }, false);
	    xhr.upload.addEventListener("error", function(e) {
		setTimeout(function() {
		    alertErr("Uploading "+file.name+" failed!");
		    closeEditor(editorId);
		}, 100);
	    }, false);	
	    xhr.upload.addEventListener("abort", ()=>closeEditor(editorId), false);
	    xhr.open("PUT", "private/command.lsp?cmd=uploadfw");
	    xhr.setRequestHeader("x-requested-with","upload")
	    xhr.send(file);
	    elems.fwdrop.hide();
	    elems.fwguage.show();
	});

	setFwGuage(90);
    }));
});
