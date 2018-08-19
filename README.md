# ccs-queryd-gui
Tomoyo Akari CCS Queryd Gui - Using Zenity - Ask Question With Gui Window

Note : This app need zenity also this tool is just a quick edit/hack to have a gui awaiting having time to develop a full gui app... so don't judge it ;)

Feature :

- Remember last 3 requests and send back the same answer
- On deny same request are answered the same (deny)
- On allow a rule is added
 
Install :

1. Install zenity 
2. Replace ccs-queryd.c 
3. Compile with make

**Start/Usage :**

Just run ccs-queryd it will trigger a window when needed to shows access requests that are about to be rejected by the kernel's decision and let you decide what to do... the question window have a 15sec timeout 

Answering no and timeout will deny access and answering yes will allow and add a granting policy

Also don't forget to run ccs-savepolicy is you want to keep modifications... 

**Get Tomoyo ccs-toos**

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

