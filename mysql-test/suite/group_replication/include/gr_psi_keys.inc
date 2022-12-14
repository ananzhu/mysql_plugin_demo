################################################################################
# Verify that performance schema keys are available in instrument table
# immediately after plugin installation.
#
# Test:
# 0. The test requires one Server: M1
# 1. Verify that performance schema keys are available in instrument table
#    immediately after plugin installation (before starting the GR).
# 2. Start GR on M1
# 3. Verify that there are no extra psi keys that were added after starting
#    the GR
# 4. Verify that performance_schema.threads list the usual
#    threads after member is ONLINE.
# 5. Test end.
################################################################################

--source include/have_group_replication_plugin.inc
--let $rpl_skip_group_replication_start= 1
--source include/group_replication.inc

--echo
--echo ############################################################
--echo # 1. verify that performance schema keys are available in instrument
--echo #    table immediately after plugin installation
--echo #    (before starting the GR).
--connection server1
SELECT * FROM performance_schema.setup_instruments WHERE NAME LIKE '%group_rpl%' AND NAME NOT LIKE 'wait/synch/%/group_rpl/GCS_%' AND NAME NOT LIKE 'memory/group_rpl/gcs_%';
--let $psi_keys_count_before_gr=`SELECT COUNT(*) FROM performance_schema.setup_instruments WHERE NAME LIKE '%group_rpl%' AND NAME NOT LIKE 'wait/synch/%/group_rpl/GCS_%' AND NAME NOT LIKE 'memory/group_rpl/gcs_%'`

--echo
--echo ############################################################
--echo # 2. Start the GR
--source include/start_and_bootstrap_group_replication.inc

--echo
--echo ############################################################
--echo # 3. Verify that there are no extra psi keys that were added
--echo #   after starting the GR
--let $psi_keys_count_after_gr=`SELECT COUNT(*) FROM performance_schema.setup_instruments WHERE NAME LIKE '%group_rpl%' AND NAME NOT LIKE 'wait/synch/%/group_rpl/GCS_%' AND NAME NOT LIKE 'memory/group_rpl/gcs_%'`
--let $assert_text= 'There should be no extra psi keys added after starting the GR'
--let $assert_cond= $psi_keys_count_before_gr = $psi_keys_count_after_gr
--source include/assert.inc

--echo
--echo ############################################################
--echo # 4. Verify that performance_schema.threads list the usual
--echo #    threads after member is ONLINE.
SELECT NAME, TYPE, INSTRUMENTED FROM performance_schema.threads WHERE NAME LIKE '%group_rpl%' ORDER BY NAME;

--echo
--echo ############################################################
--echo # 5. Test end.
--source include/group_replication_end.inc
