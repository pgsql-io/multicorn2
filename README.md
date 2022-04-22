
Multicorn2
==========

Multicorn Python3 Wrapper for Postgresql 10+ Foreign Data Wrapper

The Multicorn Foreign Data Wrapper allows you to fetch foreign data in Python in your PostgreSQL server.

Multicorn2 is distributed under the PostgreSQL license. See the LICENSE file for
details.

Using in PGSQL-IO
=================

1.) Install PGSQL.IO from the command line, in your home directory, with the curl command at the top of https://pgsql.io

2.) Change into the pgsql directory and install pgXX
      cd psql
      ./io install pg14 --start
      ./io install multicorn2
      
3.) Use multicorn as you normally would AND you can install popular FDW's that use multicorn such as ElasticSerachFDW & BigQueryFDW
      ./io install esfdw
      ./io install bqfdw
