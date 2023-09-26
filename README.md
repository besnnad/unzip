# unzip
解压 zip 文件
使用状态机分段解压 zip 文件
代码总接口：getZipInterface
  参数：const char *data: 未解密 zip 文件信息
        size_t size: zip 文件的大小
        void **zm: 记忆结构体

代码拥有解析状态，可以将一个 zip 文件分成多段，多次调用 getZipInterface 接口，获取解压信息
  详细过程可见 /main.c
