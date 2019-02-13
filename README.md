# Tomoyo - Linux Application Firewall - Gui 
# ccs-queryd-gui

Tomoyo-Linux-Application-Firewall is a gui for the kernel module Tomoyo/Akari it asks for network permission on per app basis with a gui generated with zenity; This tool is just a quick hack to have a gui awaiting having time to develop a full gui app... so don't judge it ;)

**Require :**

- Tomoyo/Akari (already setup)
- Zenity

**Feature :**

- Remember last 3 requests and send back the same answer automatically if current request is similar to one of them
- On allow (yes) a rule is added
- Notification display in addition of the question window
- Ignore profile 8 (you can use profile 8 to bypass this application "usefull for let say blocked apps")
- Auto request (15s timeout)
- Exclude many profile 
- Allow all for the current requesting application
- Deny all for the current requesting application
- 4 Buttons Yes/No/AllowAll/DenyAll
- Save policy
- Tray icon... 
etc.

**Screenshot (SeaMonkey Request - Current Available Version) :**

![Screenshot](https://raw.githubusercontent.com/intika/ccs-queryd-gui/master/Cap.png)

![Screenshot](https://raw.githubusercontent.com/intika/ccs-queryd-gui/master/Cap4.png)

![Screenshot](https://raw.githubusercontent.com/intika/ccs-queryd-gui/master/Cap3.png)

![Screenshot](https://raw.githubusercontent.com/intika/ccs-queryd-gui/master/Cap2.png)

 
**Install :**

1. Install zenity
2. Compile with make
3. ./ccs-firewall

**Start/Usage I/II :**

Just run ccs-firewall it will trigger a window when needed to shows access requests that are about to be rejected by the kernel's decision and let you decide what to do... the question window have a 15sec timeout 

Answering no and timeout will deny access and answering yes will allow and add a granting policy

Also don't forget to run ccs-savepolicy is you want to keep modifications... 

**Start/Usage II/II :**

You can use this application at startup in system tray icon to mimic classic windows firewall, here is an example used under KDE with kdocker and konsole  
```
kdocker -t -i /CORRECT-ICON-PATH/firewall.svg konsole -e sudo ccs-queryd
```

**Get Tomoyo ccs-toos :**

```
  wget -O ccs-tools-1.8.5-20170102.tar.gz 'http://osdn.jp/frs/redir.php?m=jaist&f=/tomoyo/49693/ccs-tools-1.8.5-20170102.tar.gz'
  wget -O ccs-tools-1.8.5-20170102.tar.gz.asc 'http://osdn.jp/frs/redir.php?m=jaist&f=/tomoyo/49693/ccs-tools-1.8.5-20170102.tar.gz.asc'
  gpg ccs-tools-1.8.5-20170102.tar.gz.asc
  tar -zxf ccs-tools-1.8.5-20170102.tar.gz
  cd ccs-tools/
  make -s USRLIBDIR=/usr/lib
  su
  make -s USRLIBDIR=/usr/lib install
```

https://tomoyo.osdn.jp/

