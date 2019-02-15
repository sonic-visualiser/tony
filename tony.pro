
TEMPLATE = subdirs

SUBDIRS += \
        sub_base

# We build the tests on every platform, though at the time of
# writing they are only automatically run on non-Windows platforms
# (because of the difficulty of getting them running nicely in the
# IDE without causing great confusion if a test fails).
SUBDIRS += \
        sub_test_svcore_base \
        sub_test_svcore_system \
        sub_test_svcore_data_fileio \
        sub_test_svcore_data_model

SUBDIRS += \
	sub_tony \
        sub_pyin \
        sub_chp
        
sub_base.file = base.pro
        
sub_test_svcore_base.file = test-svcore-base.pro
sub_test_svcore_system.file = test-svcore-system.pro
sub_test_svcore_data_fileio.file = test-svcore-data-fileio.pro
sub_test_svcore_data_model.file = test-svcore-data-model.pro

sub_tony.file = tonyapp.pro
sub_pyin.file = pyin.pro
sub_chp.file = chp.pro

CONFIG += ordered
