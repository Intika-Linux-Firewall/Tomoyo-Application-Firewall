# Tomoyo - Linux Application Firewall - Gui 
# ccs-queryd-gui
Tomoyo Akari CCS Queryd Gui - Using Zenity - Ask Question With Gui Window

Note : This app need zenity also this tool is just a quick edit/hack to have a gui awaiting having time to develop a full gui app... so don't judge it ;)

**Feature :**

- Remember last 3 requests and send back the same answer automatically if current request is similar to one of them
- On allow (yes) a rule is added
- Notification display in addition of the question window
- Ignore profile 8 (you can use profile 8 to bypass this application "usefull for let say blocked apps")
- Auto request (15s timeout)

**Feature Private Version :**

I am working on a private version and will release it here too as soon as possible current available feature on it 

- Exclude many profile 
- Allow all for the current requesting application
- Deny all for the current requesting application
- 4 Buttons Yes/No/AllowAll/DenyAll
- Save policy
- Tray icon... 
etc.

**Screenshot (SeaMonkey Request - Current Available Version) :**

![Screenshot](https://raw.githubusercontent.com/intika/ccs-queryd-gui/master/Cap.png)

**Screenshot (Future Version) :**

![Screenshot](https://raw.githubusercontent.com/intika/ccs-queryd-gui/master/Cap2.png)

![Screenshot](https://raw.githubusercontent.com/intika/ccs-queryd-gui/master/Cap3.png)
 
**Install :**

1. Install zenity
2. Replace ccs-queryd.c 
3. Compile with make

**Start/Usage I/II :**

Just run ccs-queryd it will trigger a window when needed to shows access requests that are about to be rejected by the kernel's decision and let you decide what to do... the question window have a 15sec timeout 

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

intika.dev@gmail.com

https://tomoyo.osdn.jp/

