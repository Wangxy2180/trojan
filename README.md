# Tips

这里是v1.12.0版本，是我用来阅读代码写笔记使用的

拿到手记得先修改配置文件里的东西，

运行方式：先配置文件检测。
```
./trojan --test
```
如果没问题会报
```
The config file looks good.
```

尚未填的坑
- [ ] po::command_line_parser(argc, argv).options(desc).positional(pd).run()
- []
- []
- []



# trojan

[![Build Status](https://travis-ci.org/trojan-gfw/trojan.svg?branch=master)](https://travis-ci.org/trojan-gfw/trojan) [![Build Status](https://ci.appveyor.com/api/projects/status/e0ulqwb44i7j5gkl/branch/master?svg=true)](https://ci.appveyor.com/project/GreaterFire/trojan/branch/master)

An unidentifiable mechanism that helps you bypass GFW.

Trojan features multiple protocols over `TLS` to avoid both active/passive detections and ISP `QoS` limitations.

Trojan is not a fixed program or protocol. It's an idea, an idea that imitating the most common service, to an extent that it behaves identically, could help you get across the Great FireWall permanently, without being identified ever. We are the GreatER Fire; we ship Trojan Horses.

## Documentations

An online documentation can be found [here](https://trojan-gfw.github.io/trojan/).  
Installation guide on various platforms can be found in the [wiki](https://github.com/trojan-gfw/trojan/wiki/Binary-&-Package-Distributions).

## Dependencies

- [CMake](https://cmake.org/) >= 3.7.2
- [Boost](http://www.boost.org/) >= 1.54.0
- [OpenSSL](https://www.openssl.org/) >= 1.0.2
- [libmysqlclient](https://dev.mysql.com/downloads/connector/c/)

## License

[GPLv3](LICENSE)
