/* Deep Sleep widget (1.3) javascript logic */
/* M. Beaumel (cochonou@fastmail.fm), 09/08/10 */

/*
* Copyright (C) 2010 M. Beaumel
*
* This file is part of Deep Sleep.
*
* Deep Sleep is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* Deep Sleep is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with Deep Sleep. If not, see <http://www.gnu.org/licenses/>.
*/

function setup() {
	thePassButton = new AppleGlassButton(document.getElementById("passButton"), "OK", enterPass);
	theConfigButton = new AppleGlassButton(document.getElementById("configButton"), "OK", enterConfig);
	theInfoButton = new AppleInfoButton(document.getElementById("infoButton"), document.getElementById("front"), "white", "white", info);
	debugMsg = false;
	sleepMsg = false;
	busy = false;

	fader[0] = new fadeObj(0, 'sleepText', '666666', 'ffffff', 15, 15, false);
	fader[0].message[1] = "Deep Sleep";

	fader[1] = new fadeObj(1, 'clickText', '666666', 'ffffff', 15, 15, false);
	fader[1].message[1] = "click to enter";

	fader[2] = new fadeObj(2, 'msgText', '666666', 'ffffff', 15, 15, false);
	fader[2].message[1] = "System is going down to sleep...";

	front = document.getElementById("front");
	back = document.getElementById("backPass");
	config = document.getElementById("backConfig")
	nameField = document.getElementById("nameField");
	passField = document.getElementById("passField");
	regularDropDown = document.getElementById("regularDropDown");
	deepDropDown = document.getElementById("deepDropDown");
}

function showText() {
	fade(0, 1, true);
	fade(1, 1, true);
}

function hideText() {
	fade(0, 1, false);
	fade(1, 1, false);
	if (debugMsg) {
		fade(2, 2, false);
		debugMsg = false;
	}
	if (sleepMsg) {
		fade(2, 1, false);
		sleepMsg = false;
	}
}

function showBackPass() {
    var name = widget.system("/usr/bin/whoami", null).outputString;
    name = name.substring(0, name.length-1);
    nameField.value = name;

    if (window.widget)
       	widget.prepareForTransition("ToBack");

    front.style.display="none";
    back.style.display="block";

    if (window.widget)
        setTimeout ('widget.performTransition();', 0);

	passField.focus();
    busy = false;
}

function showBackConfig() {
	if (busy) return;
	var original = modeToIndex(parseInt(widget.system("pmset -g | grep hibernatemode | tail -c 2", null).outputString));
	regularDropDown.selectedIndex = original;

	var targetMode;
	if (!widget.preferenceForKey("DeepSleepMode")) {
		targetMode = "soft";
	} else {
		targetMode = widget.preferenceForKey("DeepSleepMode");
	}
	var target = stringToIndex(targetMode);
	deepDropDown.selectedIndex = target;

    if (window.widget)
       	widget.prepareForTransition("ToBack");

    front.style.display="none";
    config.style.display="block";

    if (window.widget)
        setTimeout ('widget.performTransition();', 0);

    busy = false;
}

function hideBack() {
    if (window.widget)
        widget.prepareForTransition("ToFront");

    back.style.display="none";
	config.style.display="none";
    front.style.display="block";

    if (window.widget)
        setTimeout ('widget.performTransition();', 0);
}

function passKeyPressed(event) {
	if (event.keyCode == 13)
		enterPass();
}

function enterPass() {
	var name = nameField.value;
	var password = passField.value;
	passField.value = "";
	widget.system("/bin/chmod o+x deepsleep", null);
	var command = "/usr/bin/osascript -e 'do shell script \"/usr/sbin/chown root:wheel deepsleep && /bin/chmod u+s deepsleep\" user name \"" + name + "\" password \"" + password + "\" with administrator privileges'";
	widget.system(command, null);
	hideBack();
}

function enterConfig() {
	widget.setPreferenceForKey(deepDropDown.options[deepDropDown.selectedIndex].value, "DeepSleepMode");
	var regularMode = regularDropDown.options[regularDropDown.selectedIndex].value;
	var command = "./deepsleep -bos ";
	command += regularMode;
	widget.system(command, null);
	hideBack();
}

function goSleep() {
	var targetMode;
	if (!widget.preferenceForKey("DeepSleepMode")) {
		targetMode = "soft";
	} else {
		targetMode = widget.preferenceForKey("DeepSleepMode");
	}
	var command = "./deepsleep -bm ";
	command += targetMode;
	var syscmd = widget.system(command, function() {;});
	syscmd.onreadoutput = function (output) {fade(2,1,false); sleepMsg=false; fader[2].message[2]=output; fade(2,2,true); debugMsg=true;};
	setTimeout(function () {busy = false}, 10000);
}

function activation() {
	 if (busy) return;
	 busy = true;
	 if ( ArePermsOK() ) {
	 	fade(2, 1, true);
		sleepMsg = true;
		setTimeout(goSleep, 350);
	 } else {
		if (IsFileVaultEnabled()) {
			fader[2].message[2] = "Install the widget in root Library folder";
			fade(2, 2, true);
			debugMsg = true;
		} else {
			showBackPass();
		}
	}
}

function info() {
	if ( ArePermsOK() ) {
 		showBackConfig();
 	} else {
		if (IsFileVaultEnabled()) {
			fader[2].message[2] = "Install the widget in root Library folder";
			fade(2, 2, true);
			debugMsg = true;
		} else {
			showBackPass();
		}
	}
}

function ArePermsOK() {
	var ls = widget.system("/bin/ls -l deepsleep | awk '{print $1 $3}'", null).outputString;
	var ownerIsRoot = ls.indexOf("root", 0);
	var setuid = ls.charAt(3);
	if ( (ownerIsRoot != -1) && (setuid.toLowerCase() == "s") ) return true;
	else return false;
}

function IsFileVaultEnabled() {
	var fileVault = widget.system("/bin/bash fvault.sh", null).outputString;
	if (fileVault.indexOf("protected") != -1) return true;
	else return false;
}


function modeToString(mode) {
	var str;
	switch(mode) {
	case 0:
		str = "hard";
  		break
	case 1:
		str = "soft";
		break
	case 3:
		str = "dump";
		break
	case 5:
		str = "soft";
		break
	case 7:
		str = "dump";
		break
	default:
		str = "hard";
	}
	return str;
}

function stringToIndex(str) {
	var index;
	if (str.indexOf("hard") != -1)
		index = 0;
	if (str.indexOf("dump") != -1)
		index = 1;
	if (str.indexOf("soft") != -1)
		index = 2;
	return index;
}

function modeToIndex(mode) {
	var index;
	switch(mode) {
	case 0:
		index = 0;
  		break
	case 1:
		index = 2;
		break
	case 3:
		index = 1;
		break
	case 5:
		index = 2;
		break
	case 7:
		index = 1;
		break
	default:
		index = 0;
	}
	return index;
}
