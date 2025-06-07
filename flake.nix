{
  description = "multicorn2 nix development environment";

  inputs = {
    nixpkgs.url = "nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system: let
      debugBuild = false;
      multicornVersion = "3.1";

      pkgs = nixpkgs.legacyPackages.${system};

      requiredPythonPackages = ps: (
        # Such that we pass the UNSUPPORTS_SQLALCHEMY check in Makefile and can run the SQLAlchemy tests
        [ ps.sqlalchemy ] ++ ps.sqlalchemy.optional-dependencies.postgresql
      );

      devPostgresql = pkgs.postgresql_17.overrideAttrs (oldAttrs: {} // pkgs.lib.optionalAttrs debugBuild { dontStrip = true; }); # If debug symbols are needed.
      devPython = pkgs.python313.withPackages (ps: (requiredPythonPackages ps));

      testPythonVersions = with pkgs; [
        # python39 # end of security support is scheduled for 2025-10-31; therefore nixpkgs support was dropped before nixos 25.05 was released
        # python310 # error: sphinx-8.2.3 not supported for interpreter python3.10
        python311
        python312
        python313
      ];
      testPostgresVersions = with pkgs; [
        postgresql_13
        postgresql_14
        postgresql_15
        postgresql_16
        postgresql_17
      ];
      testVersionCombos = pkgs.lib.cartesianProduct {
        python = testPythonVersions;
        postgres = testPostgresVersions;
      };
      makeTestSuiteName = { python, postgres }: let
          pgMajorVersion = builtins.head (builtins.split "\\." postgres.version);
        in
          "test_pg${pgMajorVersion}_py${builtins.replaceStrings ["."] [""] python.pythonVersion}";

      makeMulticornPostgresExtension = target_python: target_postgresql: pkgs.stdenv.mkDerivation {
        pname = "multicorn2";
        version = multicornVersion;

        # Local source directory, but only including the files necessary for building the Postgres extension...
        src = [
          ./src
          ./sql
          ./Makefile
          ./multicorn.control
        ];
        unpackPhase = ''
          for srcFile in $src; do
            echo "cp $srcFile ..."
            cp -r $srcFile $(stripHash $srcFile)
          done
          chmod -R +w .
        '';

        buildInputs = [
          target_python
        ];
        nativeBuildInputs = [
          target_postgresql.pg_config
          pkgs.clang
        ];
        installPhase = ''
          runHook preInstall
          install -D multicorn${target_postgresql.dlSuffix} -t $out/lib/
          install -D sql/multicorn--''${version#v}.sql -t $out/share/postgresql/extension
          install -D multicorn.control -t $out/share/postgresql/extension
          runHook postInstall
        '';

        separateDebugInfo = true;
      };

      makeMulticornPythonPackage = target_python: target_postgresql: target_python.pkgs.buildPythonPackage rec {
        pname = "multicorn2-python";
        version = multicornVersion;

        # Local source directory, but only including the files necessary for building the Python extension...
        src = [
          ./src/utils.c
          ./src/multicorn.h
          ./python
          ./setup.py
          ./pyproject.toml
          ./multicorn.control
        ];
        unpackPhase = ''
          for srcFile in $src; do
            echo "cp $srcFile ..."
            cp -r $srcFile $(stripHash $srcFile)
          done
          mkdir -p src
          mv *.[ch] src/
          chmod -R +w .
        '';

        nativeBuildInputs = [
          target_postgresql.pg_config
        ];

        separateDebugInfo = true;
      };

      makePostgresWithPlPython = test_python: test_postgresql:
        (test_postgresql.override {
          # write_filesystem.sql uses plpython3u, so, we enable it... This causes 25 rebuilds of postgresql (each PG
          # version * each Python version)... it might be worth either removing / rewriting this test suite,
          # making it optional, or using at least one consistent version of python for the plpython3u build... or
          # just using a new custom build cache to avoid rebuilds.
          pythonSupport = true;
          python3 = test_python;
        });

      makeTestSuite = test_python: test_postgresql:
      let
        # "Build order", so to speak...
        # 1. Multicorn python package first, using the "raw" Python & "raw" PostgreSQL
        # 2. Python enhanced w/ the multicorn package
        # 3. PostgreSQL w/ plpython3, using "enhanced" Python
        # 4. Multicorn postgresql extension, using the "enhanced" Python & plpython3 PostgreSQL
        # 5. PostgreSQL w/ plpython3 + multicorn extension

        multicornPython = (makeMulticornPythonPackage test_python test_postgresql);
        enhancedPython = (test_python.withPackages (ps:
          [multicornPython]
          ++
          (requiredPythonPackages ps)
        ));
        pythonEnabledPostgres = (makePostgresWithPlPython enhancedPython test_postgresql);
        multicornPostgresExtension = (makeMulticornPostgresExtension enhancedPython pythonEnabledPostgres);
        postgresqlWithMulticorn = pythonEnabledPostgres.withPackages (ps: [
          pythonEnabledPostgres.plpython3
          multicornPostgresExtension
        ]);
      in pkgs.stdenv.mkDerivation {
        name = "multicorn2-python-test";

        phases = [ "unpackPhase" "checkPhase" "installPhase" ];
        doCheck = true;

        # Local source directory, but only including the files necessary for running the regression tests...
        src = [
          ./Makefile
          ./test-3.9
          ./test-3.10
          ./test-3.11
          ./test-3.12
          ./test-3.13
          ./test-common
        ];
        unpackPhase = ''
          for srcFile in $src; do
            echo "cp $srcFile ..."
            cp -r $srcFile $(stripHash $srcFile)
          done
        '';

        nativeCheckInputs = [
          postgresqlWithMulticorn
          postgresqlWithMulticorn.pg_config
          enhancedPython
        ];
        checkPhase = ''
          runHook preCheck

          # Verify that we can load sqlalchemy & psycopg2 before we attempt tests, otherwise `UNSUPPORTS_SQLALCHEMY`
          # will quietly be set and the tests will skipped silently.
          echo "Verifying sqlalchemy is installed..."
          python -c "import sqlalchemy;import psycopg2"

          # Verify that pg_config is available, otherwise pgxs may not be available to be loaded into the Makefile,
          # which will result in pg_regress_check being undefined, which will result in an empty command, which will
          # result in tests exiting and being ignored from `easycheck`.
          echo "Verifying pg_config is accessible..."
          pg_config --version

          # Verifying that `multicorn` python module can be accessed by the in-PATH python.
          echo "Verifying that multicorn python module is accessible..."
          python -c "import multicorn"

          set +e
          make easycheck
          RESULT=$?
          set -e
          if [[ $RESULT -ne 0 ]]; then
            echo "easycheck failed"
            cat /build/regression.diffs
            exit $RESULT
          fi

          runHook postCheck
        '';
        installPhase = "touch $out";
      };

    in {
      devShells.default = pkgs.mkShell {
        buildInputs = [
          devPython
          devPostgresql
          devPostgresql.pg_config
        ];
      };

      packages = rec {
        # Tests probably shouldn't be in "packages"?  But flake schema doesn't seem to support a separate "tests"
        # output...

        # The true targets for tests are either for a specific PG & Python version, or for all of them.
        # eg:
        # nix build .#testSuites.test_pg12_py39
        # nix build .#allTestSuites
        testSuites = pkgs.lib.foldl' (acc: combo:
            let
              name = makeTestSuiteName combo;
              testSuite = makeTestSuite combo.python combo.postgres;
            in
            acc // { ${name} = testSuite; }
          ) {} testVersionCombos;

        allTestSuites = (pkgs.stdenv.mkDerivation {
          name = "multicorn2-test-all";
          buildInputs = builtins.attrValues testSuites;
          phases = [ "installPhase" ];
          installPhase = "touch $out";
        });

        # The postgresWithPlPython steps are used so that we can build Postgres instances that include the the
        # plpython3u extension.  This isn't the default in Nixpkgs, so we need to build it ourselves.  But because we have
        # to build a lot of them (one each for each version of python and postgresql), we create a separate target so that
        # we can build them and then push them to a binary cache.
        # eg.
        # nix build .#postgresWithPlPython.postgres-test_pg12_py39
        # nix build .#allPostgresWithPlPython

        postgresWithPlPython = pkgs.lib.foldl' (acc: combo:
            let
              name = "postgres-${makeTestSuiteName combo}";
              testSuite = makePostgresWithPlPython combo.python combo.postgres;
            in
            acc // { ${name} = testSuite; }
          ) {} testVersionCombos;

        allPostgresWithPlPython = (pkgs.stdenv.mkDerivation {
          name = "multicorn2-postgres-all";
          buildInputs = builtins.attrValues postgresWithPlPython;
          phases = [ "installPhase" ];
          # In order to make sure that allPostgresWithPlPython can be pushed to a binary cache and include all the built
          # Postgres instances, the installPhase creates symlinks to the built instances.
          installPhase = ''
            mkdir -p $out
            ${pkgs.lib.concatStringsSep "\n" (pkgs.lib.mapAttrsToList (name: outPath: ''
              ln -s ${outPath} $out/${name}
            '') postgresWithPlPython)}
            touch $out/placeholder
          '';
        });
      };
    });
}
