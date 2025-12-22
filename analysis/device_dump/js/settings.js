var res;
//天气
var city1=getE("city1");
var city2=getE("city2");
var weather_interval=getE("weather_interval");
var t_u=getE("temp");
var w_u=getE("windspeed");
var p_u=getE("pressure");
var key=getE("api");
//设置
var brt=getE("brt");
//联网
var ssid = getE('ssid');
var pwd = getE('pwd');
var scan_pwd = getE('scan_pwd');
var scan_ssid = getE('scan_ssid');
var delay=getE("delay");
//时间
var ntp = getE("ntp");
var h = getE("hour12");
var time_interval = getE("time_interval");
var hc = getE("hc");
var mc = getE("mc");
var sc = getE("sc");
var yr = getE("yr");
var mth = getE("mth");
var day = getE("day");
var day_fmt = getE("day-format");
var colon = getE("colon");
var dst = getE("dst");
var font = getE("font");
//相册
var autoplay = getE("autoplay");
var i_i = getE("image_interval");
//股票
var stock = getE("stock");
var exchange = getE("exchange");
var c2 = getE("c2");
var stock_interval = getE("stock_interval");
var s_l = getE("s_loop");
//加密货币
var cin0 = getE("cin0");
var cin1 = getE("cin1");
var cin2 = getE("cin2");
var coin_interval = getE("coin_interval");
var c_l = getE("c_loop");
//B站
var bili_uid = getE("bili_uid");
var bili_interval = getE("bili_interval");
//监视器
var ip = getE("ip");
var m_i = getE("m_i");
document.title = "GeekMagic Weather Clock";
var theme = getE("theme");

function getData(data) {
	getResponse(data, function(responseText) {
	try {
        res = JSON.parse(responseText);
    } catch(e) {
		return;
    }
	console.log(res);
	//天气
	if(res.cd) city1.value = res.cd;
	//if(res.cd && city2 && !isNaN(res.cd)) city2.value = res.cd;
	if(res.w_i) weather_interval.value = res.w_i;
	if(res.t_u) t_u.value = res.t_u;
	if(res.w_u) w_u.value = res.w_u;
	if(res.p_u) p_u.value = res.p_u;
	if(res.key) key.value = res.key;

	
	//设置
	if(res.brt) brt.value = res.brt;
	
	var t1 = getE("time1");
	var t2 = getE("time2");
	var time_brt = getE("time_brt");
	var time_brt_en = getE("time_brt_en");
	if(res.t1) t1.value = res.t1;
	if(res.t2) t2.value = res.t2;
	if(res.en) time_brt_en.checked = true;
	if(res.b2) time_brt.value = res.b2;
	var model = getE("model");
	if(model)
	if(res.m && res.v) model.innerHTML = "Model: "+res.m+",Version: "+res.v;

	//联网
	if(res.a) {
		scan_ssid.value = res.a;//scan_ssid
		scan_pwd.value = res.p;
	}
	if(res.delay) delay.value = res.delay;
	
	//时间
	if(res.ntp) ntp.value = res.ntp;
	if(res.h == "1") h.checked = true;
	if(res.t_i) time_interval.value = res.t_i;
	if(res.hc) hc.value = res.hc;
	if(res.mc) mc.value = res.mc;
	if(res.sc) sc.value = res.sc;
	if(res.colon) colon.checked = true;
	var dayfmt = getE("day-format");
	if(res.day_fmt) dayfmt.value = res.day_fmt;
	if(res.dst) dst.checked = true;
	if(res.font) font.value = res.font;
	
	//天数倒计时
	if(res.yr) yr.value = res.yr;
	if(res.mth) mth.value = res.mth;
	if(res.day) day.value = res.day;
	
	//相册
	if(res.autoplay) autoplay.checked = res.autoplay;
	if(res.i_i) image_interval.value = res.i_i;
	
	//股票
	if(res.c0) c0.value = res.c0;
	if(res.c1) c1.value = res.c1;
	if(res.c2) c2.value = res.c2;
	if(res.s_i) stock_interval.value = res.s_i;
	if(res.s_l == "1") s_l.checked = true;
	
	//加密货币
	if(res.cin0) cin0.value = res.cin0;
	if(res.cin1) cin1.value = res.cin1;
	if(res.cin2) cin2.value = res.cin2;
	if(res.c_i) coin_interval.value = res.c_i;
	if(res.c_l == "1") c_l.checked = true;
	
	//B站
	if(res.b_i) bili_interval.value = res.b_i;
	if(res.uid) bili_uid.value = res.uid;
	
	//股票
	if(res.code) stock.value = res.code;
	if(res.exchange) exchange.value = res.exchange;

	//监视器
	if(res.m_i) m_i.value = res.m_i;
	if(res.ip) ip.value = res.ip;	
	
	if(res.list){
		if(res.list[0] == "1"){getE("th1").checked = true;}
		if(res.list[2] == "1"){getE("th2").checked = true;}
		if(res.list[4] == "1"){getE("th3").checked = true;}
		if(res.list[6] == "1"){getE("th4").checked = true;}
		if(res.list[8] == "1"){getE("th5").checked = true;}
		if(res.list[10] == "1"){getE("th6").checked = true;}
		if(res.list[12] == "1"){getE("th7").checked = true;}
		if(res.list[14] == "1"){getE("th8").checked = true;}
		if(res.list[16] == "1"){getE("th9").checked = true;}
		if(res.sw_en == "1") {getE("sw_en").checked = true;}
		if(res.sw_i) {getE("theme_interval").value = res.sw_i;}
	} 
});
}
function send_http(url){
	getResponse(url, function(responseText) {
		if (responseText == "OK") {
      showPopup("Saved successfully!", 1500, "#02a601");
		}else{
			showPopup("Save failed!", 2000, "#dc0d04");
		} 
	});
}
function set_c(){
	var copyright = getE("copyright");
	if(copyright != null)copyright.innerHTML = '<br />Copyright (c) 2025 GeekMagic® All rights reserved, Support mail:<a href=\"\" target=\"_blank\"> ifengchao1314@gmail.com</a>';
}
set_c();
function getE(name){
	return document.getElementById(name);
}
function getNav(){
	var navLinks = [
		{ href: "network.html", text: "Network" },
		{ href: "weather.html", text: "Weather" },
		{ href: "time.html", text: "Time" },
		{ href: "image.html", text: "Pictures" },
		//{ href: "stock.html", text: "Stocks" },
		//{ href: "daytimer.html", text: "Countdown" },
		//{ href: "bili.html", text: "B站粉" },
		//{ href: "monitor.html", text: "电脑性能监视器" },
		{ href: "settings.html", text: "Settings" }
	];

	var dynamicNav = getE("nav");
	dynamicNav.innerHTML = "";
	for (var i = 0; i < navLinks.length; i++) {
		var link = document.createElement("a");
		link.className = "center";
		link.href = navLinks[i].href;
		link.textContent = navLinks[i].text;
		dynamicNav.appendChild(link);
	}
}
function escapeHTML(str) {
	if(str==undefined) return;
    return str
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/\"/g, '&quot;')
      .replace(/\'/g, '&#39;')
      .replace(/\//g, '&#x2F;')
}
function showPopup(message, closeAfter,bg) {
	const popup = document.getElementById('popup');
	popup.textContent = message;
	popup.style.opacity = '1';
	popup.style.backgroundColor = bg;

	setTimeout(() => {
		popup.style.opacity = '0';
	}, closeAfter); // 2秒后自动消失
}

function getResponse(adr, callback, timeoutCallback, timeout, method){
	if(timeoutCallback === undefined) {
		timeoutCallback = function(){
			showPopup("Device connection lost, check connection and reload please... ", 2000, "#dc0d04");
		};
	}
	if(timeout === undefined) timeout = 10000; 
	if(method === undefined) method = "GET";
	var xmlhttp = new XMLHttpRequest();
	xmlhttp.onreadystatechange = function() {
		if(xmlhttp.readyState == 4){
			if(xmlhttp.status == 200){
				callback(xmlhttp.responseText);
			}
			else if(xmlhttp.status == 404){
			}
			else timeoutCallback();
		}
	};
	xmlhttp.open(method, adr, true);
	xmlhttp.send();
	xmlhttp.timeout = timeout;
	xmlhttp.ontimeout = timeoutCallback;
}