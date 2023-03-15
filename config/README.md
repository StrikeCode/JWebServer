## 配置解析模块

### getopt

```cpp
int getopt(int argc, char * const argv[], const char *optstring);
 
extern char *optarg;
extern int optind, opterr, optopt;
```

**参数说明**

argc：通常由 main 函数直接传入，表示**参数的数量**
argv：通常也由 main 函数直接传入，表示**参数的字符串变量数组**
optstring：一个包含**正确的参数选项字符串，**用于参数的解析。例如 “abc:d::”，其中 -a，-b 就表示两个普通选项，-c 表示一个必须有参数（有没有空格都行）的选项，因为它后面有一个冒号。-d 表示一个可选参数，即-d后面接不接参数都行，但是若接参数中间不能有空格如

```sh
./server -d11 // ok
./server -d //ok
./server -d 11 // no!
```



> 可以没有 -a -b -c 选项，但如果写了-c 就一定要有参数，因为其后面有:34

**全局变量**

optarg：如果某个选项有参数，这包含当前选项的参数字符串
optind：argv 的当前索引值
opterr：正常运行状态下为 0。非零时表示存在无效选项或者缺少选项参数，并输出其错误信息
optopt：当发现无效选项字符时，即 getopt() 方法**返回 ? 字符**，optopt 中包含的就是发现的无效选项字符