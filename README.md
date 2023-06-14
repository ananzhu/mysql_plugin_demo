# mysql_plugin_demo

## 1、介绍

在mysql中完成了一个存储引擎，数据结构：表内容单链表存储，索引art树存储

## 2、配置环境

可以使用docker配置ubuntu环境，我用的ubuntu版本为ubuntu20.04

```dockerfile
# 使用Ubuntu 20.04作为基础镜像
FROM ubuntu:20.04

# 设置非交互模式，防止在docker build过程中出现交互式问题
ENV DEBIAN_FRONTEND=noninteractive

# 更新并安装所需的软件包
RUN apt-get update && apt-get install -y \
    cmake \
    g++ \
    openssl \
    libtirpc-dev \
    libudev-dev \
    libssl-dev \
    libncurses5-dev \
    flex \
    bison \
    pkg-config \
    vim \
    git \
    gdb \
    autoconf \
    automake \
    libtool \
    curl \
    make \
    unzip \
    openssh-server # 新增，安装sshd服务

# protobuf版本可能需要手动下载和编译
# 下载protobuf
RUN apt-get install -y wget && \
    wget https://github.com/protocolbuffers/protobuf/releases/download/v21.6/protobuf-all-21.6.tar.gz

# 解压、编译和安装protobuf
RUN tar -xzvf protobuf-all-21.6.tar.gz && \
    cd protobuf-21.6/ && \
    ./configure && \
    make -j 15 && \
    make install -j 15 && \
    ldconfig

# 清理无用的软件包和缓存
RUN apt-get clean 

# 配置sshd服务
RUN mkdir /var/run/sshd
RUN echo 'root:YOUR_ROOT_PASSWORD' | chpasswd # 修改为你需要设置的root用户密码
RUN sed -i 's/#PermitRootLogin prohibit-password/PermitRootLogin yes/' /etc/ssh/sshd_config

# SSH登录修复
RUN sed 's@session\s*required\s*pam_loginuid.so@session optional pam_loginuid.so@g' -i /etc/pam.d/sshd

ENV NOTVISIBLE "in users profile"
RUN echo "export VISIBLE=now" >> /etc/profile

# 开放22端口
EXPOSE 22

# 默认启动sshd服务
CMD ["/usr/sbin/sshd", "-D"]
```

## 3、创建镜像

```bash
sudo docker build -t myubuntu:22.04 .
```

## 4、启动容器

```bash
sudo docker run -it -p 45899:22 -p 45900:3306 --name myubuntu myubuntu:20.04 /bin/bash
```

## 5、下载和编译

```bash
#进入/usr/local文件夹

cd /usr/local
```





```bash
#从github将mysql代码clone下来

git clone https://github.com/pupupupupi/mysql_plugin_demo.git
```





```bash
#在/usr/local/目录下创建mysql文件夹，

#并在/usr/local/msyql目录下创建data文件夹和install文件夹，根据以下命令创建对应文件夹

cd /usr/local

mkdir mysql

cd /usr/local/mysql

mkdir data

mkdir install
```





```bash
#在mysql_plugin_demo目录下，建立目录名为build的文件夹

cd /usr/local/mysql_plugin_demo



sudo mkdir build		

cd build



#在/usr/local/local/mysql_plugin_demo/build文件夹输入以下配置命令

sudo cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local/mysql/install \

        -DMYSQL_DATADIR=/usr/local/mysql/data \

        -DSYSCONFDIR=/etc -DMYSQL_TCP_PORT=3306 \

        -DMYSQL_UNIX_ADDR=/usr/local/mysql/mysql.sock \

        -DWITH_EXTRA_CHARSETS=all \

        -DDEFAULT_CHARSET=utf8mb4 \

        -DDEFAULT_COLLATION=utf8mb4_unicode_ci \

        -DWITH_INNOBASE_STORAGE_ENGINE=1 \

        -DWITH_MYISAM_STORAGE_ENGINE=1 \

        -DWITH_ARCHIVE_STORAGE_ENGINE=1 \

        -DWITH_FEDERATED_STORAGE_ENGINE=1 \

        -DWITH_BLACKHOLE_STORAGE_ENGINE=1 \

        -DWITH_PERFSCHEMA_STORAGE_ENGINE=1 \

        -DWITH_SPARTAN_STORAGE_ENGINE=1 \

        -DENABLED_LOCAL_INFILE=1 \

        -DWITH_DEBUG=1 \

        -DMYSQL_MAINTAINER_MODE=0 \

        -DWITH_EMBEDDED_SERVER=0 \

        -DINSTALL_SHAREDIR=share \

        -DDOWNLOAD_BOOST=1 \

        -DWITH_BOOST=/usr/local/  \

        -DFORCE_INSOURCE_BUILD=1
```

## 6、配置和安装

```bash
#创建mysql用户

sudo useradd mysql
```





```bash
#给mysql用户添加登陆密码

sudo passwd mysql
```





```bash
#进入/usr/local/mysql/install/bin目录

cd /usr/local/mysql/install/bin

#使用mysqld初始化mysql

./mysqld --initialize-insecure --user=mysql
```





```bash
#使用root账号给mysql用户授权

sudo chmod -R 777 /usr/local/mysql
```





```bash
#使用mysql用户新开一个终端

su - mysql

#进入/usr/local/mysql/install/bin

cd /usr/local/mysql/install/bin

#启动mysql服务

./mysqld --skip-log-bin
```





```bash
#使用root账号启动mysql

su

#输入密码

cd /usr/local/mysql/install/bin

#没有密码只需要按enter就可以
./mysql -u root -p

#修改mysql的root用户启动密码

alter user root@localhost identified by '你的密码';
```





# mysql_plugin_demo

## 1. Introduction

A storage engine has been developed in MySQL with the following data structures: table content stored as a singly linked list, and art tree used for indexing.

## 2. Environment Setup

You can configure the Ubuntu environment using Docker. The Ubuntu version used is ubuntu20.04.

```dockerfile
# Use Ubuntu 20.04 as the base image
FROM ubuntu:20.04

# Set non-interactive mode to prevent interactive prompts during Docker build
ENV DEBIAN_FRONTEND=noninteractive

# Update and install required software packages
RUN apt-get update && apt-get install -y \
    cmake \
    g++ \
    openssl \
    libtirpc-dev \
    libudev-dev \
    libssl-dev \
    libncurses5-dev \
    flex \
    bison \
    pkg-config \
    vim \
    git \
    gdb \
    autoconf \
    automake \
    libtool \
    curl \
    make \
    unzip \
    openssh-server # Add sshd service installation

# Protobuf version may need to be manually downloaded and compiled
# Download Protobuf
RUN apt-get install -y wget && \
    wget https://github.com/protocolbuffers/protobuf/releases/download/v21.6/protobuf-all-21.6.tar.gz

# Extract, compile, and install Protobuf
RUN tar -xzvf protobuf-all-21.6.tar.gz && \
    cd protobuf-21.6/ && \
    ./configure && \
    make -j 15 && \
    make install -j 15 && \
    ldconfig

# Clean up unused software packages and cache
RUN apt-get clean 

# Configure sshd service
RUN mkdir /var/run/sshd
RUN echo 'root:YOUR_ROOT_PASSWORD' | chpasswd # Change to the desired root user password
RUN sed -i 's/#PermitRootLogin prohibit-password/PermitRootLogin yes/' /etc/ssh/sshd_config

# Fix SSH login
RUN sed 's@session\s*required\s*pam_loginuid.so@session optional pam_loginuid.so@g' -i /etc/pam.d/sshd

ENV NOTVISIBLE "in users profile"
RUN echo "export VISIBLE=now" >> /etc/profile

# Expose port 22
EXPOSE 22

# Start sshd service by default
CMD ["/usr/sbin/sshd", "-D"]
```

## 3. Create Image

```bash
sudo docker build -t myubuntu:20.04 .
```

## 4. Start Container

```bash
sudo docker run -it -p 45899:22 -p 45900:3306 --name myubuntu myubuntu:20.04 /bin/bash
```

## 5. Download and Compile

```bash
# Go to the /usr/local directory
cd /usr/local

# Clone the MySQL code from GitHub
git clone https://github.com/pupupupupi/mysql_plugin_demo.git

# Create directories in /usr/local: mysql, data, and install
cd /usr/local
mkdir mysql
cd /usr/local/mysql
mkdir data
mkdir install
```





```bash
# In the mysql_plugin_demo directory, create a directory named 'build'
cd /usr/local/mysql_plugin_demo
sudo mkdir build
cd build

# Configure the build in the /usr/local/mysql_plugin_demo/build directory with the following command
sudo cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local/mysql/install \

        -DMYSQL_DATADIR=/usr/local/mysql/data \

        -DSYSCONFDIR=/etc -DMYSQL_TCP_PORT=3306 \

        -DMYSQL_UNIX_ADDR=/usr/local/mysql/mysql.sock \

        -DWITH_EXTRA_CHARSETS=all \

        -DDEFAULT_CHARSET=utf8mb4 \

        -DDEFAULT_COLLATION=utf8mb4_unicode_ci \

        -DWITH_INNOBASE_STORAGE_ENGINE=1 \

        -DWITH_MYISAM_STORAGE_ENGINE=1 \

        -DWITH_ARCHIVE_STORAGE_ENGINE=1 \

        -DWITH_FEDERATED_STORAGE_ENGINE=1 \

        -DWITH_BLACKHOLE_STORAGE_ENGINE=1 \

        -DWITH_PERFSCHEMA_STORAGE_ENGINE=1 \

        -DWITH_SPARTAN_STORAGE_ENGINE=1 \

        -DENABLED_LOCAL_INFILE=1 \

        -DWITH_DEBUG=1 \

        -DMYSQL_MAINTAINER_MODE=0 \

        -DWITH_EMBEDDED_SERVER=0 \

        -DINSTALL_SHAREDIR=share \

        -DDOWNLOAD_BOOST=1 \

        -DWITH_BOOST=/usr/local/  \

        -DFORCE_INSOURCE_BUILD=1
```





## 6. Configuration and Installation

```bash
# Create the mysql user
sudo useradd mysql

# Set a login password for the mysql user
sudo passwd mysql

# Go to the /usr/local/mysql/install/bin directory
cd /usr/local/mysql/install/bin

# Initialize MySQL using mysqld
./mysqld --initialize-insecure --user=mysql

# Grant permissions to the mysql user using the root account
sudo chmod -R 777 /usr/local/mysql

# Open a new terminal as the mysql user
su - mysql

# Go to the /usr/local/mysql/install/bin directory
cd /usr/local/mysql/install/bin

# Start the MySQL service
./mysqld --skip-log-bin

# Start MySQL using the root account
su

# Enter the password

# Go to the /usr/local/mysql/install/bin directory
cd /usr/local/mysql/install/bin

#none password just enter
./mysql -u root -p

# Modify the startup password for the root user in MySQL
alter user root@localhost identified by 'your_password';
```



## 7、实验结果

![结果1](https://pupupupupi-md.oss-cn-beijing.aliyuncs.com/%E7%BB%93%E6%9E%9C1.png)





![结果2](https://pupupupupi-md.oss-cn-beijing.aliyuncs.com/%E7%BB%93%E6%9E%9C2.png)





![结果3](https://pupupupupi-md.oss-cn-beijing.aliyuncs.com/%E7%BB%93%E6%9E%9C3.png)





![1插入](https://pupupupupi-md.oss-cn-beijing.aliyuncs.com/1%E6%8F%92%E5%85%A5.png)





![2更新](https://pupupupupi-md.oss-cn-beijing.aliyuncs.com/2%E6%9B%B4%E6%96%B0.png)





![3索引查找](https://pupupupupi-md.oss-cn-beijing.aliyuncs.com/3%E7%B4%A2%E5%BC%95%E6%9F%A5%E6%89%BE.png)





![4删除](https://pupupupupi-md.oss-cn-beijing.aliyuncs.com/4%E5%88%A0%E9%99%A4.png)
