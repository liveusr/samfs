# samfs
simple distributed file system
==============================
server file : samd.c | client file : masd.c
-------------------------------------------

Running Simple Distributed File System client-server using openstack/amazon cloud instance

- Create a cloud instance as described in earlier sections

- Install FUSE library on your local machine (not on cloud instance)

  $ sudo apt-get install libfuse-dev

- Download/copy File System code on your local machine

- Extract code from the archive and compile

  $ unzip samfs.zip

  $ cd samfs

  $ make

- Copy server binary i.e. ‘samd’ to cloud instance

  $ scp samd ubuntu@10.0.0.2:~/

- Run server binary on cloud instance

  $ ./samd –export 10.0.0.2 /home/ubuntu/

- Here in above command, sever exports its home directory which clients can access

- Create a new directory on your local machine to mount server directory

- Mount server directory to above created local file system directory

  $ mkdir /tmp/dst

  $ ./masd –mount 10.0.0.2 /tmp/dst

- Now whatever operations you perform on ‘/tmp/dst’ directory on client, they will be served by server on cloud instance

