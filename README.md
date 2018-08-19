# ccs-queryd-gui
Tomoyo Akari CCS Queryd Gui - Using Zenity - Ask Question With Gui Window

Note : This app need zenity also this tool is just a quick edit/hack to have a gui awaiting having time to develop a full gui app... so don't judge it ;)
 
1. Install zenity 
2. Replace ccs-queryd.c 
3. Compile with make

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

