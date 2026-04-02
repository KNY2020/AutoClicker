# AutoClicker
项目基于vscode + esp-idf插件开发 原型为esp-idf的example中esp_hid_device开发 调试流程：

打开vscode中的esp-idf插件，选择esp-idf版本，此版使用最新的ESP-IDF V6.0 release
用typeC线连接esp32c3开发板，esp-idf中选择ComX - ESP32C3(QFN32)，X为电脑分配端口号
设置esp-idf的target板为：ESP32C3
设置SDK配置编辑器：Serial flasher config项中修改以下几项： Flash SPI mode设置为DIO， Flash SPI speed 为80MHz，Flash size为4MB，其余不用修改点保存退出配置编辑器
选择烧录方式为UART
clone项目到本地打开，点击火苗标志(ESP-IDF构建、烧录和调试)，等待代码编译完成下载到板端
