<?xml version="1.0"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <!-- NOTE: This needs to point to the DocBook XSL Stylesheet fo/docbook.xsl file -->
  <xsl:import href="/Applications//xxe-std-30/XMLEditor.app/Contents/Resources/addon/config/docbook/xsl/fo/docbook.xsl" />
	<xsl:include href="titlepage.xsl" />
  <xsl:param name="fop.extensions" select="1" />
  <xsl:param name="insert.xref.page.number" select="yes" />
	<xsl:param name="draft.mode" select="no" />
</xsl:stylesheet>
