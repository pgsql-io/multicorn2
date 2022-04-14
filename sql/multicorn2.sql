-- create wrapper with validator and handler
CREATE OR REPLACE FUNCTION multicorn2_validator (text[], oid)
RETURNS bool
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

CREATE OR REPLACE FUNCTION multicorn2_handler ()
RETURNS fdw_handler
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

CREATE FOREIGN DATA WRAPPER multicorn2
VALIDATOR multicorn2_validator HANDLER multicorn2_handler;
