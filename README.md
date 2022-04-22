
Multicorn2
==========

Multicorn Python3 Wrapper for Postgresql 10+ Foreign Data Wrapper

The Multicorn Foreign Data Wrapper allows you to fetch foreign data in Python in your PostgreSQL server.

Multicorn2 is distributed under the PostgreSQL license. See the LICENSE file for
details.

## Using in PGSQL-IO

1.) Install PGSQL.IO from the command line, in your home directory, with the curl command at the top of https://pgsql.io

2.) Change into the pgsql directory and install pgXX
```bash
cd psql
./io install pg14 --start
./io install multicorn2
```
      
3.) Use multicorn as you normally would AND you can install popular FDW's that use multicorn such as ElasticSerachFDW & BigQueryFDW
```bash
      ./io install esfdw
      ./io install bqfdw
```

## Building Multicorn2 from Source

It is built the same way all standard postgres extensions are built with following dependcies needing to be installed:

### Install Dependencies for Building Postgres & then the Multicorn2 extension
On Debian/Ubuntu systems:
```bash
sudo apt install -y build-essential libreadline-dev zlib1g-dev flex bison libxml2-dev libxslt-dev libssl-dev libxml2-utils xsltproc
sudo apt install -y python3 python3-dev python3-setuptools python3-pip
```

On CentOS/Rocky/Redhat systems:
```bash
sudo yum install -y bison-devel readline-devel libedit-devel zlib-devel openssl-devel bzip2-devel libmxl2-devel libxslt-devel wget
sudo yum groupinstall -y 'Development Tools'

sudo yum -y install git python3 python3-devel python3-pip
```

### Upgrade to latest PIP (recommended)
```bash
cd ~
wget https://bootstrap.pypa.io/get-pip.py
sudo python3 get-pip.py
rm get-pip.py
```
