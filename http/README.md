### 关键功能

- 实现cgi登录注册功能

- 实现http接收解析请求报文， 封装发送响应报文

### 对http请求报文的解析

对http请求报文的解析利用了状态机，**从状态机负责读取报文的一行，主状态机负责对该行数据进行解析**，主状态机内部调用从状态机，从状态机驱动主状态机。

- GET 和 POST请求报文的区别之一是有无消息体部分，GET请求没有消息体，当解析完空行后，便完成了报文解析。



`http_conn::HTTP_CODE http_conn::process_read()` 函数中如下循环条件：

```cpp
while(((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK)) 
        || ((line_status == parse_line()) == LINE_OK))
```

含义为：

在GET请求报文中，每一行都是\r\n作为结束，所以对报文进行拆解时，仅用从状态机的状态`(line_status=parse_line())==LINE_OK`语句即可。

但在POST请求报文中，消息体的末尾没有任何字符，所以不能使用从状态机的状态，这里转而使用主状态机的状态作为循环入口条件。

解析完消息体后，报文的完整解析就完成了，但此时主状态机的状态还是`CHECK_STATE_CONTENT`，也就是说，符合循环入口条件，还会再次进入循环，这并不是我们所希望的。

为此，增加了该语句，并**在完成消息体解析后，将line_status变量更改为LINE_OPEN，**此时可以跳出循环，完成报文解析任务。



