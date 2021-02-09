# Makino2020
OP-TEE用プログラム

1. **hello_world_ta.c**  
Secure Worldで実行されるプログラム(TA: Turusted Application)
/opteeをインストールしたディレクトリ/optee_examples/hello_world/ta/　に置きます  

2. **main.c**  
Normal Worldから実行するプログラム  
/opteeをインストールしたディレクトリ/optee_examples/hello_world/host/　に置きます  

3. **optee_rpi3**  
Raspberry Pi 3 MOdel BをPCとUART接続し、ターミナルからRaspberry Piを起動する際のコマンド用スクリプト  
Raspberry PiのログをPC内に保存できる。起動時刻がファイル名に含まれるようにした  
picocom -b 115200 /dev/ttyUSB0 と一々打つのが面倒だったので作成した  

4. **soc_term.c**  
エミュレーターであるQemuのNormal WorldとSecure Worldのログを保存するためのプログラム  
 ※ エラー処理を全く行ってません  
