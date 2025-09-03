SET client_min_messages=NOTICE;
CREATE EXTENSION multicorn;
CREATE server multicorn_srv foreign data wrapper multicorn options (
    wrapper 'multicorn.testfdw.TestForeignDataWrapper'
);
CREATE user mapping FOR current_user server multicorn_srv options (usermapping 'test');

CREATE foreign table testmulticorn (
    test1 date,
    test2 timestamp
) server multicorn_srv options (
    option1 'option1',
    test_type 'date',
    canlimit 'true',
    cansort 'true'
);

CREATE foreign table testmulticorn_nolimit(
    test1 date,
    test2 timestamp
) server multicorn_srv options (
    option1 'option1',
    test_type 'date',
    canlimit 'false',
    cansort 'true'
);

CREATE foreign table testmulticorn_nosort(
    test1 date,
    test2 timestamp
) server multicorn_srv options (
    option1 'option1',
    test_type 'date',
    canlimit 'true',
    cansort 'false'
);

-- Verify limit and offset are not pushed down when FWD says "no" (but still returns the 1 correct row)
EXPLAIN SELECT * FROM testmulticorn_nolimit LIMIT 1 OFFSET 1;
SELECT * FROM testmulticorn_nolimit LIMIT 1 OFFSET 1;

-- Verify limit and offset are pushed down when FWD says "yes" (returns just 1 row with proper cost)
EXPLAIN SELECT * FROM testmulticorn LIMIT 1 OFFSET 1;
SELECT * FROM testmulticorn LIMIT 1 OFFSET 1;

-- Verify limit and offset w/ sort not pushed down (still returns the 1 correct row)
EXPLAIN SELECT * FROM testmulticorn_nolimit ORDER BY test1 LIMIT 1 OFFSET 1;
SELECT * FROM testmulticorn_nolimit ORDER BY test1 LIMIT 1 OFFSET 1;

-- Verify limit and offset w/ sort are pushed down (returns just 1 row with proper cost)
EXPLAIN SELECT * FROM testmulticorn ORDER BY test1 LIMIT 1 OFFSET 1;
SELECT * FROM testmulticorn ORDER BY test1 LIMIT 1 OFFSET 1;

-- Verify limit and offset are not pushed when sort is not pushed down
EXPLAIN SELECT * FROM testmulticorn_nosort ORDER BY test1 LIMIT 1 OFFSET 1;
SELECT * FROM testmulticorn_nosort ORDER BY test1 LIMIT 1 OFFSET 1;

DROP USER MAPPING FOR current_user SERVER multicorn_srv;
DROP EXTENSION multicorn cascade;
