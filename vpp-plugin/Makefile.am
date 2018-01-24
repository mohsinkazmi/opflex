#
# renderer-vpp: Vpp OpFlex agent renderer plugin
# Copyright (c) 2018 Cisco Systems, Inc. and others.  All rights reserved.
#
# This program and the accompanying materials are made available under the
# terms of the Eclipse Public License v1.0 which accompanies this distribution,
# and is available at http://www.eclipse.org/legal/epl-v10.html
#
###########
#
# Process this file with automake to produce a Makefile.in

ACLOCAL_AMFLAGS = -I m4

VPP_ADDL_CFLAGS = \
        -D__STDC_LIMIT_MACROS \
        -D__STDC_CONSTANT_MACROS \
        -I/usr/include/vpp_plugins \
        -fpermissive

# Create a convenience library containing our plugin sources
noinst_LTLIBRARIES = librenderer_vpp.la
librenderer_vpp_la_CXXFLAGS = \
	$(libopflex_agent_CFLAGS) \
        $(VPP_ADDL_CFLAGS) \
        -Ivpp/include
librenderer_vpp_la_LIBADD = \
	$(libopflex_agent_LIBS)

noinst_HEADERS = \
	vpp/include/VppInspect.h \
	vpp/include/VppLogHandler.h \
	vpp/include/VPPRenderer.h \
	vpp/include/VppUplink.h \
	vpp/include/VppVirtualRouter.h \
	vpp/include/VppManager.h

librenderer_vpp_la_SOURCES = \
	vpp/VPPRenderer.cpp \
        vpp/VppUplink.cpp \
        vpp/VppManager.cpp \
        vpp/VppVirtualRouter.cpp \
        vpp/VppLogHandler.cpp \
        vpp/VppInspect.cpp

# Link the convenience library into an installable module
lib_LTLIBRARIES = libopflex_agent_renderer_vpp.la
libopflex_agent_renderer_vpp_la_LIBADD = \
	librenderer_vpp.la
libopflex_agent_renderer_vpp_la_SOURCES =
libopflex_agent_renderer_vpp_la_LDFLAGS = \
	-avoid-version -module -shared

# Install the default plugin configuration file
pluginconfdir=$(sysconfdir)/opflex-agent-ovs/plugins.conf.d
pluginconf_DATA = plugin-renderer-vpp.conf

# Create a unit test driver that links to the plugin convenience
# library
TESTS = vpp_test
noinst_PROGRAMS = $(TESTS)
vpp_test_CXXFLAGS = \
        -I$(top_srcdir)/lib/include \
        -I$(top_srcdir)/cmd/test/include \
        -Ivpp/include \
	$(libopflex_agent_CFLAGS) \
        $(VPP_ADDL_CFLAGS) \
	-DBOOST_TEST_DYN_LINK
vpp_test_LDADD = \
        $(BOOST_FILESYSTEM_LIB) \
        $(BOOST_SYSTEM_LIB) \
	$(libopflex_agent_LIBS) \
	$(BOOST_UNIT_TEST_FRAMEWORK_LIB) \
        librenderer_vpp.la
vpp_test_SOURCES = \
	vpp/test/vpp_test.cpp \
	vpp/test/VPPRenderer_test.cpp \
        vpp/test/VppManager_test.cpp
