/*
 * Copyright 2012 Red Hat Inc., Durham, North Carolina.
 * All Rights Reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software 
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors:
 *      Martin Preisler <mpreisle@redhat.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "public/scap_ds.h"
#include "public/xccdf_benchmark.h"
#include "public/oval_definitions.h"
#include "public/oscap.h"
#include "public/oscap_text.h"

#include "ds_common.h"

#include "common/alloc.h"
#include "common/_error.h"
#include "common/util.h"
#include "common/list.h"
#include "common/oscap_acquire.h"

#include <sys/stat.h>
#include <time.h>
#include <libgen.h>

#include <libxml/xmlreader.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#ifndef MAXPATHLEN
#   define MAXPATHLEN 1024
#endif

static const char* datastream_ns_uri = "http://scap.nist.gov/schema/scap/source/1.2";
static const char* xlink_ns_uri = "http://www.w3.org/1999/xlink";
static const char* cat_ns_uri = "urn:oasis:names:tc:entity:xmlns:xml:catalog";
static const char* sce_xccdf_ns_uri = "http://open-scap.org/page/SCE_xccdf_stream";

static xmlNodePtr node_get_child_element(xmlNodePtr parent, const char* name)
{
	xmlNodePtr candidate = parent->children;

	for (; candidate != NULL; candidate = candidate->next)
	{
		if (candidate->type != XML_ELEMENT_NODE)
			continue;

		if (name != NULL && strcmp((const char*)(candidate->name), name) != 0)
			continue;

		return candidate;
	}

	return NULL;
}

static xmlNodePtr ds_sds_find_component_ref(xmlNodePtr datastream, const char* id)
{
	/* This searches for a ds:component-ref (XLink) element with a given id.
	 * It returns a first such element in a given ds:data-stream.
	 */
	xmlNodePtr cref_parent = datastream->children;

	for (; cref_parent != NULL; cref_parent = cref_parent->next)
	{
		if (cref_parent->type != XML_ELEMENT_NODE)
			continue;

		xmlNodePtr component_ref = cref_parent->children;

		for (; component_ref != NULL; component_ref = component_ref->next)
		{
			if (component_ref->type != XML_ELEMENT_NODE)
				continue;

			if (strcmp((const char*)(component_ref->name), "component-ref") != 0)
				continue;

			char* cref_id = (char*)xmlGetProp(component_ref, BAD_CAST "id");
			if (strcmp(cref_id, id) == 0)
			{
				xmlFree(cref_id);
				return component_ref;
			}
			xmlFree(cref_id);
		}
	}

	return NULL;
}

static xmlNodePtr _lookup_component_in_collection(xmlDocPtr doc, const char *component_id)
{
	xmlNodePtr root = xmlDocGetRootElement(doc);
	xmlNodePtr component = NULL;
	xmlNodePtr candidate = root->children;

	for (; candidate != NULL; candidate = candidate->next)
	{
		if (candidate->type != XML_ELEMENT_NODE)
			continue;

		if ((strcmp((const char*)(candidate->name), "component") != 0) &&
		    (strcmp((const char*)(candidate->name), "extended-component") != 0))
			continue;

		char* candidate_id = (char*)xmlGetProp(candidate, BAD_CAST "id");
		if (strcmp(candidate_id, component_id) == 0)
		{
			component = candidate;
			xmlFree(candidate_id);
			break;
		}
		xmlFree(candidate_id);
	}
	return component;
}

static int ds_sds_dump_component(const char* component_id, xmlDocPtr doc, const char* filename)
{
	xmlNodePtr component = _lookup_component_in_collection(doc, component_id);
	if (component == NULL)
	{
		oscap_seterr(OSCAP_EFAMILY_XML, "Component of given id '%s' was not found in the document.", component_id);
		return -1;
	}

	xmlNodePtr inner_root = node_get_child_element(component, NULL);

	if (inner_root == NULL)
	{
		oscap_seterr(OSCAP_EFAMILY_XML, "Found component (id='%s') but it has no element contents, nothing to dump, skipping...", component_id);
		return -1;
	}

	// If the inner root is script, we have to treat it in a special way
	if (strcmp((const char*)inner_root->name, "script") == 0) {
		if (inner_root->children) {
			// TODO: should we check whether the component is extended?
			int fd;
			xmlChar* text_contents = xmlNodeGetContent(inner_root->children);
			if ((fd = open(filename, O_CREAT | O_TRUNC | O_NOFOLLOW | O_WRONLY, 0700)) < 0) {
				oscap_seterr(OSCAP_EFAMILY_XML, "Error while creating script component (id='%s') to file '%s'.", component_id, filename);
				xmlFree(text_contents);
				return -1;
			}
			FILE* output_file = fdopen(fd, "w");
			if (output_file == NULL) {
				oscap_seterr(OSCAP_EFAMILY_XML, "Error while dumping script component (id='%s') to file '%s'.", component_id, filename);
				xmlFree(text_contents);
				close(fd);
				return -1;
			}
			// TODO: error checking, fprintf should return strlen((const char*)text_contents)
			fprintf(output_file, "%s", text_contents ? (char*)text_contents : "");
			// NB: This code is for SCE scripts
			if (fchmod(fd, 0700) != 0) {
				oscap_seterr(OSCAP_EFAMILY_XML, "Failed to set executable permission on script (id='%s') that was split to '%s'.", component_id, filename);
			}

			fclose(output_file);
			xmlFree(text_contents);
		}
		else {
			oscap_seterr(OSCAP_EFAMILY_XML, "Error while dumping script component (id='%s') to file '%s'. "
				"The script element was empty!", component_id, filename);
			return -1;
		}
	}
	// Otherwise we create a new XML doc we will dump the contents to.
	// We can't just dump node "innerXML" because namespaces have to be
	// handled.
	else {
		xmlDOMWrapCtxtPtr wrap_ctxt = xmlDOMWrapNewCtxt();

		xmlDocPtr new_doc = xmlNewDoc(BAD_CAST "1.0");
		xmlNodePtr res_node = NULL;
		if (xmlDOMWrapCloneNode(wrap_ctxt, doc, inner_root, &res_node, new_doc, NULL, 1, 0) != 0)
		{
			oscap_seterr(OSCAP_EFAMILY_XML, "Error when cloning node while dumping component (id='%s').", component_id);
			xmlFreeDoc(new_doc);
			xmlDOMWrapFreeCtxt(wrap_ctxt);
			return -1;
		}
		xmlDocSetRootElement(new_doc, res_node);
		if (xmlDOMWrapReconcileNamespaces(wrap_ctxt, res_node, 0) != 0)
		{
			oscap_seterr(OSCAP_EFAMILY_XML, "Internal libxml error when reconciling namespaces while dumping component (id='%s').", component_id);
			xmlFreeDoc(new_doc);
			xmlDOMWrapFreeCtxt(wrap_ctxt);
			return -1;
		}
		if (xmlSaveFileEnc(filename, new_doc, "utf-8") == -1)
		{
			oscap_seterr(OSCAP_EFAMILY_GLIBC, "Error when saving resulting DOM to file '%s' while dumping component (id='%s').", filename, component_id);
			xmlFreeDoc(new_doc);
			xmlDOMWrapFreeCtxt(wrap_ctxt);
			return -1;
		}
		xmlFreeDoc(new_doc);

		xmlDOMWrapFreeCtxt(wrap_ctxt);
	}

	return 0;
}

static int ds_sds_dump_component_ref_as(xmlNodePtr component_ref, xmlDocPtr doc, xmlNodePtr datastream, const char* target_dir, const char* filename)
{
	char* cref_id = (char*)xmlGetProp(component_ref, BAD_CAST "id");
	if (!cref_id)
	{
		oscap_seterr(OSCAP_EFAMILY_XML, "No or invalid id attribute on given component-ref.");
		xmlFree(cref_id);
		return -1;
	}

	char* xlink_href = (char*)xmlGetNsProp(component_ref, BAD_CAST "href", BAD_CAST xlink_ns_uri);
	if (!xlink_href || strlen(xlink_href) < 2)
	{
		oscap_seterr(OSCAP_EFAMILY_XML, "No or invalid xlink:href attribute on given component-ref.");
		xmlFree(cref_id);
		xmlFree(xlink_href);
		return -1;
	}

	assert(xlink_href[0] == '#');
	const char* component_id = xlink_href + 1 * sizeof(char);
	char* filename_cpy = oscap_sprintf("./%s", filename);
	char* file_reldir = dirname(filename_cpy);

	// the cast is safe to do because we are using the GNU basename, it doesn't
	// modify the string
	const char* file_basename = basename((char*)filename);

	const char* target_filename_dirname = oscap_sprintf("%s/%s", target_dir, file_reldir);
	if (ds_common_mkdir_p(target_filename_dirname) == -1)
	{
		oscap_seterr(OSCAP_EFAMILY_GLIBC, "Error making directory '%s' while dumping component to file '%s'.", target_dir, filename);
		xmlFree(cref_id);
		xmlFree(xlink_href);
		oscap_free(target_filename_dirname);
		oscap_free(filename_cpy);
		return -1;
	}
	const char* target_filename = oscap_sprintf("%s/%s/%s", target_dir, file_reldir, file_basename);
	ds_sds_dump_component(component_id, doc, target_filename);
	oscap_free(target_filename);
	oscap_free(filename_cpy);

	xmlNodePtr catalog = node_get_child_element(component_ref, "catalog");
	if (catalog)
	{
		xmlNodePtr uri = catalog->children;

		for (; uri != NULL; uri = uri->next)
		{
			if (uri->type != XML_ELEMENT_NODE)
				continue;

			if (strcmp((const char*)(uri->name), "uri") != 0)
				continue;

			char* name = (char*)xmlGetProp(uri, BAD_CAST "name");

			if (!name)
			{
				oscap_seterr(OSCAP_EFAMILY_XML, "No 'name' attribute for a component referenced in the catalog of component '%s'.", component_id);
				xmlFree(cref_id);
				xmlFree(xlink_href);
				oscap_free(target_filename_dirname);
				return -1;
			}

			char* str_uri = (char*)xmlGetProp(uri, BAD_CAST "uri");

			if (!str_uri || strlen(str_uri) < 2)
			{
				oscap_seterr(OSCAP_EFAMILY_XML, "No or invalid 'uri' attribute for a component referenced in the catalog of component '%s'.", component_id);
				xmlFree(str_uri);
				xmlFree(name);
				xmlFree(cref_id);
				xmlFree(xlink_href);
				oscap_free(target_filename_dirname);
				return -1;
			}

			// the pointer arithmetics simply skips the first character which is '#'
			assert(str_uri[0] == '#');
			xmlNodePtr cat_component_ref = ds_sds_find_component_ref(datastream, str_uri + 1 * sizeof(char));

			if (!cat_component_ref)
			{
				oscap_seterr(OSCAP_EFAMILY_XML, "component-ref with given id '%s' wasn't found in the document! We are looking for it because it's in the catalog of component '%s'.", str_uri + 1 * sizeof(char), component_id);
				xmlFree(str_uri);
				xmlFree(name);
				xmlFree(cref_id);
				xmlFree(xlink_href);
				oscap_free(target_filename_dirname);
				return -1;
			}
			xmlFree(str_uri);

			if (ds_sds_dump_component_ref_as(cat_component_ref, doc, datastream, target_filename_dirname, name) != 0)
			{
				xmlFree(name);
				xmlFree(cref_id);
				xmlFree(xlink_href);
				oscap_free(target_filename_dirname);
				return -1; // no need to call oscap_seterr here, it's already set
			}

			xmlFree(name);
		}
	}

	oscap_free(target_filename_dirname);
	xmlFree(cref_id);
	xmlFree(xlink_href);

	return 0;
}

static int ds_sds_dump_component_ref(xmlNodePtr component_ref, xmlDocPtr doc, xmlNodePtr datastream, const char* target_dir)
{
	char* cref_id = (char*)xmlGetProp(component_ref, BAD_CAST "id");
	if (!cref_id)
	{
		oscap_seterr(OSCAP_EFAMILY_XML, "No or invalid id attribute on given component-ref.");
		xmlFree(cref_id);
		return -1;
	}

	int result = ds_sds_dump_component_ref_as(component_ref, doc, datastream, target_dir, cref_id);
	xmlFree(cref_id);

	// if result is -1, oscap_seterr was already called, no need to call it again
	return result;
}

static xmlNodePtr _lookup_datastream_in_collection(xmlDocPtr doc, const char *datastream_id)
{
	xmlNodePtr root = xmlDocGetRootElement(doc);

	xmlNodePtr datastream = NULL;
	xmlNodePtr candidate_datastream = root->children;

	for (; candidate_datastream != NULL; candidate_datastream = candidate_datastream->next)
	{
		if (candidate_datastream->type != XML_ELEMENT_NODE)
			continue;
		if (strcmp((const char*)(candidate_datastream->name), "data-stream") != 0)
			continue;

		// at this point it is sure to be a <data-stream> element

		char* candidate_id = (char*)xmlGetProp(candidate_datastream, BAD_CAST "id");
		if (datastream_id == NULL || oscap_streq(datastream_id, candidate_id)) {
			datastream = candidate_datastream;
			xmlFree(candidate_id);
			break;
		}
		xmlFree(candidate_id);
	}

	return datastream;
}

int ds_sds_decompose_custom(const char* input_file, const char* id, const char* target_dir,
		const char* container_name, const char* component_id, const char* target_filename)
{
	xmlDocPtr doc = xmlReadFile(input_file, NULL, 0);

	if (!doc)
	{
		oscap_seterr(OSCAP_EFAMILY_XML, "Could not read/parse XML of given input file at path '%s'.", input_file);
		return -1;
	}

	xmlNodePtr datastream = _lookup_datastream_in_collection(doc, id);
	if (!datastream)
	{
		const char* error = id ?
			oscap_sprintf("Could not find any datastream of id '%s'", id) :
			oscap_sprintf("Could not find any datastream inside the file");

		oscap_seterr(OSCAP_EFAMILY_XML, error);
		oscap_free(error);
		xmlFreeDoc(doc);
		return -1;
	}

	xmlNodePtr container = node_get_child_element(datastream, container_name);

	if (!container)
	{
		if (!id)
			oscap_seterr(OSCAP_EFAMILY_XML, "No '%s' container element found in file '%s' in the first datastream.", container_name, input_file);
		else
			oscap_seterr(OSCAP_EFAMILY_XML, "No '%s' container element found in file '%s' in datastream of id '%s'.", container_name, input_file, id);

		xmlFreeDoc(doc);
		return -1;
	}

	xmlNodePtr component_ref = container->children;

	for (; component_ref != NULL; component_ref = component_ref->next)
	{
		if (component_ref->type != XML_ELEMENT_NODE)
			continue;

		if (strcmp((const char*)(component_ref->name), "component-ref") != 0)
			continue;

		xmlChar* cref_id = xmlGetProp(component_ref, BAD_CAST "id");
		// if cref_id is zero we have encountered a fatal error that will be handled
		// in ds_sds_dump_component_ref
		if (component_id && cref_id && strcmp(component_id, (char*)cref_id) != 0)
		{
			xmlFree(cref_id);
			continue;
		}
		xmlFree(cref_id);

		int result;

		if (target_filename == NULL)
		{
			result = ds_sds_dump_component_ref(component_ref, doc, datastream, strcmp(target_dir, "") == 0 ? "." : target_dir);
		}
		else
		{
			result = ds_sds_dump_component_ref_as(component_ref, doc, datastream, strcmp(target_dir, "") == 0 ? "." : target_dir, target_filename);
		}

		if (result != 0)
		{
			// oscap_seterr was already called in ds_sds_dump_component[_as]
			xmlFreeDoc(doc);
			return -1;
		}
	}

	xmlFreeDoc(doc);
	return 0;
}

int ds_sds_decompose(const char* input_file, const char* id, const char* xccdf_id, const char* target_dir, const char* xccdf_filename)
{
	return ds_sds_decompose_custom(input_file, id, target_dir, "checklists", xccdf_id, xccdf_filename);
}

static int ds_sds_compose_add_component_internal(xmlDocPtr doc, xmlNodePtr datastream, const char* filepath, const char* comp_id, bool extended)
{
	xmlNsPtr ds_ns = xmlSearchNsByHref(doc, datastream, BAD_CAST datastream_ns_uri);
	if (!ds_ns)
	{
		oscap_seterr(OSCAP_EFAMILY_GLIBC,
				"Unable to find namespace '%s' in the XML DOM tree when create "
				"source datastream. This is most likely an internal error!",
				datastream_ns_uri);
		return -1;
	}

	char file_timestamp[32];
	strcpy(file_timestamp, "0000-00-00T00:00:00");

	struct stat file_stat;
	if (stat(filepath, &file_stat) == 0)
		strftime(file_timestamp, 32, "%Y-%m-%dT%H:%M:%S", localtime(&file_stat.st_mtime));
	else {
		oscap_seterr(OSCAP_EFAMILY_GLIBC, "Could not find file %s: %s.", filepath, strerror(errno));
		// Return positive number, indicating less severe problem.
		// Rationale: When an OVAL file is missing during a scan it it not considered
		// to be deal breaker (it shall have 'notchecked' result), thus we shall allow
		// DataStreams with missing OVAL.
		return 1;
	}

	xmlNodePtr component = xmlNewNode(ds_ns, BAD_CAST (extended ? "extended-component" : "component"));
	xmlSetProp(component, BAD_CAST "id", BAD_CAST comp_id);
	xmlSetProp(component, BAD_CAST "timestamp", BAD_CAST file_timestamp);

	xmlDocPtr component_doc = xmlReadFile(filepath, NULL, 0);

	if (!component_doc)
	{
		if (!extended) {
			oscap_seterr(OSCAP_EFAMILY_XML, "Could not read/parse XML of given input file at path '%s'.", filepath);
			xmlFreeNode(component);
			return -1;
		}

		FILE* f = fopen(filepath, "r");
		if (!f) {
			oscap_seterr(OSCAP_EFAMILY_GLIBC, "Can't read plain text from file '%s'.", filepath);
			return -1;
		}

		fseek(f, 0, SEEK_END);
		long int length = ftell(f);
		fseek(f, 0, SEEK_SET);
		if (length >= 0) {
			char* buffer = oscap_alloc((length + 1) * sizeof(char));
			if (fread(buffer, length, 1, f) != 1) {
				oscap_seterr(OSCAP_EFAMILY_GLIBC, "Error while reading from file '%s'.", filepath);
				fclose(f);
				oscap_free(buffer);
				return -1;
			}
			fclose(f);
			buffer[length] = '\0';
			xmlNsPtr local_ns = xmlNewNs(component, BAD_CAST sce_xccdf_ns_uri, BAD_CAST "oscap-sce-xccdf-stream");
			xmlNewTextChild(component, local_ns, BAD_CAST "script", BAD_CAST buffer);
			oscap_free(buffer);
		}
		else {
			oscap_seterr(OSCAP_EFAMILY_GLIBC, "No data read from file '%s'.", filepath);
			fclose(f);
			return -1;
		}
	}
	else {
		xmlNodePtr component_root = xmlDocGetRootElement(component_doc);

		xmlDOMWrapCtxtPtr wrap_ctxt = xmlDOMWrapNewCtxt();

		xmlNodePtr res_component_root = NULL;
		if (xmlDOMWrapCloneNode(wrap_ctxt, component_doc, component_root, &res_component_root, doc, NULL, 1, 0) != 0)
		{
			oscap_seterr(OSCAP_EFAMILY_XML,
					"Cannot clone node when adding component from file '%s' with id '%s' while "
					"creating source datastream.", filepath, comp_id);

			xmlDOMWrapFreeCtxt(wrap_ctxt);
			xmlFreeDoc(component_doc);
			xmlFreeNode(component);

			return -1;
		}
		if (xmlDOMWrapReconcileNamespaces(wrap_ctxt, res_component_root, 0) != 0)
		{
			oscap_seterr(OSCAP_EFAMILY_XML,
					"Cannot reconcile namespaces when adding component from file '%s' with id '%s' while "
					"creating source datastream.", filepath, comp_id);

			xmlDOMWrapFreeCtxt(wrap_ctxt);
			xmlFreeDoc(component_doc);
			xmlFreeNode(component);

			return -1;
		}

		xmlAddChild(component, res_component_root);

		xmlDOMWrapFreeCtxt(wrap_ctxt);
	}

	xmlNodePtr doc_root = xmlDocGetRootElement(doc);

	if (extended)
	{
		// extended components always go at the end
		xmlAddChild(doc_root, component);
	}
	else
	{
		// this component is not extended, we have to figure out if there
		// already is an extended-component and if so, add it right before
		// that component

		xmlNodePtr first_extended_component = node_get_child_element(doc_root, "extended-component");
		if (first_extended_component == NULL)
		{
			// no extended component yet, add to the end
			xmlAddChild(doc_root, component);
		}
		else
		{
			xmlAddPrevSibling(first_extended_component, component);
		}
	}

	xmlFreeDoc(component_doc);

	return 0;
}

static int ds_sds_compose_catalog_has_uri(xmlDocPtr doc, xmlNodePtr catalog, const char* uri)
{
	xmlXPathContextPtr xpathCtx = xmlXPathNewContext(doc);
	if (xpathCtx == NULL)
	{
		oscap_seterr(OSCAP_EFAMILY_XML, "Error: unable to create a new XPath context.");
		return -1;
	}

	xmlXPathRegisterNs(xpathCtx, BAD_CAST "cat", BAD_CAST cat_ns_uri);
	xmlXPathRegisterNs(xpathCtx, BAD_CAST "xlink", BAD_CAST xlink_ns_uri);

	// limit xpath execution to just the catalog node
	// this is done for performance reasons
	xpathCtx->node = catalog;

	const char* expression = oscap_sprintf("cat:uri[@uri = '%s']", uri);

	xmlXPathObjectPtr xpathObj = xmlXPathEvalExpression(
			BAD_CAST expression,
			xpathCtx);

	oscap_free(expression);

	if (xpathObj == NULL)
	{
		oscap_seterr(OSCAP_EFAMILY_XML, "Error: Unable to evalute XPath expression.");
		xmlXPathFreeContext(xpathCtx);

		return -1;
	}

	int result = 0;

	xmlNodeSetPtr nodeset = xpathObj->nodesetval;
	if (nodeset != NULL)
		result = nodeset->nodeNr > 0 ? 0 : 1;

	xmlXPathFreeObject(xpathObj);
	xmlXPathFreeContext(xpathCtx);

	return result;
}

// takes given relative filepath and mangles it so that it's acceptable
// as a component id
static char* ds_sds_mangle_filepath(const char* filepath)
{
	if (filepath == NULL)
		return NULL;

	// the string will grow 2x the size in the worst case (every char is /)
	// TODO: We can do better than this by counting the slashes
	char* ret = oscap_alloc(strlen(filepath) * sizeof(char) * 2);

	const char* src_it = filepath;
	char* dst_it = ret;

	while (*src_it)
	{
		if (*src_it == '/')
		{
			*dst_it++ = '-';
			*dst_it++ = '-';
		}
		else
		{
			*dst_it++ = *src_it;
		}

		src_it++;
	}

	*dst_it = '\0';

	return ret;
}

static int ds_sds_compose_add_component_with_ref(xmlDocPtr doc, xmlNodePtr datastream, const char* filepath, const char* cref_id);

static inline const char *_get_dep_xpath_for_type(int document_type)
{
	static const char *xccdf_xpath = "//*[local-name() = 'check-content-ref']";
	static const char *cpe_xpath = "//*[local-name() = 'check']";
	if (document_type == OSCAP_DOCUMENT_CPE_DICTIONARY)
		return cpe_xpath;
	return xccdf_xpath;
}

static int ds_sds_compose_add_component_dependencies(xmlDocPtr doc, xmlNodePtr datastream, const char* filepath, xmlNodePtr catalog, int component_type)
{
	xmlDocPtr component_doc = xmlReadFile(filepath, NULL, 0);
	if (component_doc == NULL)
	{
		oscap_seterr(OSCAP_EFAMILY_XML, "Error: unable to read XCCDF from file '%s' while creating source datastream.", filepath);
		return -1;
	}

	xmlXPathContextPtr xpathCtx = xmlXPathNewContext(component_doc);
	if (xpathCtx == NULL)
	{
		oscap_seterr(OSCAP_EFAMILY_XML, "Error: unable to create new XPath context.");
		xmlFreeDoc(component_doc);
		return -1;
	}

	//xmlXPathRegisterNs(xpathCtx, BAD_CAST "cdf11", BAD_CAST "http://checklists.nist.gov/xccdf/1.1");
	//xmlXPathRegisterNs(xpathCtx, BAD_CAST "cdf12", BAD_CAST "http://checklists.nist.gov/xccdf/1.2");

	xmlXPathObjectPtr xpathObj = xmlXPathEvalExpression(
			// we want robustness and support for future versions, this expression
			// retrieves check-content-refs from any namespace
			BAD_CAST _get_dep_xpath_for_type(component_type),
			xpathCtx);
	if (xpathObj == NULL)
	{
		oscap_seterr(OSCAP_EFAMILY_XML, "Error: Unable to evalute XPath expression.");
		xmlXPathFreeContext(xpathCtx);
		xmlFreeDoc(component_doc);

		return -1;
	}

	xmlNsPtr cat_ns = xmlSearchNsByHref(doc, datastream, BAD_CAST cat_ns_uri);

	xmlNodeSetPtr nodeset = xpathObj->nodesetval;
	if (nodeset != NULL)
	{
		struct oscap_htable *exported = oscap_htable_new();
		char* filepath_cpy = oscap_strdup(filepath);
		const char* dir = dirname(filepath_cpy);

		for (int i = 0; i < nodeset->nodeNr; i++)
		{
			xmlNodePtr node = nodeset->nodeTab[i];

			if (node->type != XML_ELEMENT_NODE)
				continue;

			if (xmlHasProp(node, BAD_CAST "href"))
			{
				char* href = (char*)xmlGetProp(node, BAD_CAST "href");
				if (oscap_htable_get(exported, href) != NULL) {
					// This path has been already exported. Do not export duplicate.
					xmlFree(href);
					continue;
				}
				oscap_htable_add(exported, href, "");

				if (oscap_acquire_url_is_supported(href)) {
					/* If the referenced component is remote one, do not include
					 * it within the DataStream. Such component shall only be
					 * downloaded once the scan is run. */
					xmlFree(href);
					continue;
				}

				char* real_path = (strcmp(dir, "") == 0 || strcmp(dir, ".") == 0) ?
					oscap_strdup(href) : oscap_sprintf("%s/%s", dir, href);

				char* mangled_path = ds_sds_mangle_filepath(real_path);
				char* cref_id = oscap_sprintf("scap_org.open-scap_cref_%s", mangled_path);

				int counter = 0;
				while (ds_sds_find_component_ref(datastream, cref_id) != NULL) {
					// While the given component ID already exists in the document.
					oscap_free(cref_id);
					cref_id = oscap_sprintf("scap_org.open-scap_cref_%s%03d", mangled_path, counter++);
				}
				oscap_free(mangled_path);

				char* uri = oscap_sprintf("#%s", cref_id);

				// we don't want duplicated uri elements in the catalog
				if (ds_sds_compose_catalog_has_uri(doc, catalog, uri) == 0)
				{
					oscap_free(uri);
					oscap_free(cref_id);
					oscap_free(real_path);
					xmlFree(href);
					continue;
				}

				int ret = ds_sds_compose_add_component_with_ref(doc, datastream, real_path, cref_id);
				if (ret == 0) {
					xmlNodePtr catalog_uri = xmlNewNode(cat_ns, BAD_CAST "uri");
					xmlSetProp(catalog_uri, BAD_CAST "name", BAD_CAST href);
					xmlSetProp(catalog_uri, BAD_CAST "uri", BAD_CAST uri);
					xmlAddChild(catalog, catalog_uri);
				}

				oscap_free(cref_id);
				oscap_free(uri);
				oscap_free(real_path);
				xmlFree(href);

				if (ret < 0) {
					// oscap_seterr has already been called
					xmlFreeDoc(component_doc);
					oscap_htable_free0(exported);
					return -1;
				}

			}
		}

		oscap_htable_free0(exported);
		oscap_free(filepath_cpy);
	}

	xmlXPathFreeObject(xpathObj);
	xmlXPathFreeContext(xpathCtx);

	xmlFreeDoc(component_doc);

	return 0;
}

static int ds_sds_compose_has_component_ref(xmlDocPtr doc, xmlNodePtr datastream, const char* filepath, const char* cref_id)
{
	xmlXPathContextPtr xpathCtx = xmlXPathNewContext(doc);
	if (xpathCtx == NULL)
	{
		oscap_seterr(OSCAP_EFAMILY_XML, "Error: unable to create new XPath context.");
		return -1;
	}

	xmlXPathRegisterNs(xpathCtx, BAD_CAST "ds", BAD_CAST datastream_ns_uri);
	xmlXPathRegisterNs(xpathCtx, BAD_CAST "xlink", BAD_CAST xlink_ns_uri);

	// limit xpath execution to just the datastream node
	// this is done for performance reasons
	xpathCtx->node = datastream;

	const char* expression = oscap_sprintf("*/ds:component-ref[@xlink:href = '#%s' and @id = '%s']", filepath, cref_id);

	xmlXPathObjectPtr xpathObj = xmlXPathEvalExpression(
			BAD_CAST expression,
			xpathCtx);

	oscap_free(expression);

	if (xpathObj == NULL)
	{
		oscap_seterr(OSCAP_EFAMILY_XML, "Error: Unable to evalute XPath expression.");
		xmlXPathFreeContext(xpathCtx);

		return -1;
	}

	int result = 1;

	xmlNodeSetPtr nodeset = xpathObj->nodesetval;
	if (nodeset != NULL)
		result = nodeset->nodeNr > 0 ? 0 : 1;

	xmlXPathFreeObject(xpathObj);
	xmlXPathFreeContext(xpathCtx);

	return result;
}

int ds_sds_compose_add_component_with_ref(xmlDocPtr doc, xmlNodePtr datastream, const char* filepath, const char* cref_id)
{
	xmlNsPtr ds_ns = xmlSearchNsByHref(doc, datastream, BAD_CAST datastream_ns_uri);
	xmlNsPtr xlink_ns = xmlSearchNsByHref(doc, datastream, BAD_CAST xlink_ns_uri);
	xmlNsPtr cat_ns = xmlSearchNsByHref(doc, datastream, BAD_CAST cat_ns_uri);

	// In case we already have this component we just return, no need to add
	// it twice. We will typically have many references to OVAL files, adding
	// component for each reference would create unnecessarily huge datastreams
	int result = ds_sds_compose_has_component_ref(doc, datastream, filepath, cref_id);
	if (result == 0)
	{
		return 0;
	}

	if (result == -1)
	{
		// no need to free anything
		// oscap_seterr has already been called
		return -1;
	}

	xmlNodePtr cref_catalog = xmlNewNode(cat_ns, BAD_CAST "catalog");
	xmlNodePtr cref_parent;

	bool extended_component = false;

	oscap_document_type_t doc_type;
	// This may as well fail to detect the content but in such case the component
	// could be a valid extended-component content!
	int doc_type_result = oscap_determine_document_type(filepath, &doc_type);

	if (doc_type_result == 0 && doc_type == OSCAP_DOCUMENT_XCCDF)
	{
		cref_parent = node_get_child_element(datastream, "checklists");
		if (ds_sds_compose_add_component_dependencies(doc, datastream, filepath, cref_catalog, doc_type) != 0)
		{
			// oscap_seterr has already been called
			return -1;
		}
	}
	else if (doc_type_result == 0 && (doc_type == OSCAP_DOCUMENT_CPE_DICTIONARY || doc_type == OSCAP_DOCUMENT_CPE_LANGUAGE))
	{
		cref_parent = node_get_child_element(datastream, "dictionaries");
		if (cref_parent == NULL) {
			cref_parent = xmlNewNode(ds_ns, BAD_CAST "dictionaries");
			// The <ds:dictionaries element must as the first child of the datastream
			xmlNodePtr first_child = datastream->xmlChildrenNode;
			xmlNodePtr new_node = (first_child == NULL) ?
				xmlAddChild(datastream, cref_parent) : xmlAddPrevSibling(first_child, cref_parent);
			if (new_node == NULL) {
				oscap_seterr(OSCAP_EFAMILY_XML, "Failed to add dictionaries element to the DataStream.");
				xmlFreeNode(cref_parent);
				cref_parent = NULL;
			}
		}
		if (ds_sds_compose_add_component_dependencies(doc, datastream, filepath, cref_catalog, doc_type) != 0) {
			return -1;
		}
	}
	else if (doc_type_result == 0 && doc_type == OSCAP_DOCUMENT_OVAL_DEFINITIONS)
	{
		cref_parent = node_get_child_element(datastream, "checks");
	}
	else
	{
		// not an XCCDF file, not an OVAL file, not a dict/lang, assume it goes into extended components
		extended_component = true;
		cref_parent = node_get_child_element(datastream, "extended-components");
	}

	char* mangled_filepath = ds_sds_mangle_filepath(filepath);
	// extended components (sadly :-/) use a different ID scheme and have
	// a different element name than "normal" components
	char* comp_id = oscap_sprintf("scap_org.open-scap_%scomp_%s",
		extended_component ? "e" : "", mangled_filepath);

	int counter = 0;
	while (_lookup_component_in_collection(doc, comp_id) != NULL) {
		// While a component of the given ID already exists, generate a new one
		oscap_free(comp_id);
		comp_id = oscap_sprintf("scap_org.open-scap_%scomp_%s%03d",
			extended_component ? "e" : "", mangled_filepath, counter++);
	}

	oscap_free(mangled_filepath);

	result = ds_sds_compose_add_component_internal(doc, datastream, filepath, comp_id, extended_component);
	if (result == 0) {
		xmlNodePtr cref = xmlNewNode(ds_ns, BAD_CAST "component-ref");
		xmlAddChild(cref, cref_catalog);
		xmlSetProp(cref, BAD_CAST "id", BAD_CAST cref_id);

		const char* xlink_href = oscap_sprintf("#%s", comp_id);
		xmlSetNsProp(cref, xlink_ns, BAD_CAST "href", BAD_CAST xlink_href);
		oscap_free(xlink_href);

		if (xmlAddChild(cref_parent, cref) == NULL) {
			oscap_seterr(OSCAP_EFAMILY_XML, "Failed to add component-ref/@id='%s' to the DataStream.", cref_id);
			result = 1;
		}
	}

	oscap_free(comp_id);
	// the source data stream XSD requires either no catalog or a non-empty one
	if (cref_catalog->children == NULL)
	{
		xmlUnlinkNode(cref_catalog);
		xmlFreeNode(cref_catalog);
	}

	return result;
}

int ds_sds_compose_add_component(const char *target_datastream, const char *datastream_id, const char *new_component, bool extended)
{
	xmlDocPtr doc = xmlReadFile(target_datastream, NULL, 0);
	if (doc == NULL) {
		oscap_seterr(OSCAP_EFAMILY_XML, "Could not read/parse XML of given input file at path '%s'.", target_datastream);
		return 1;
	}
	xmlNodePtr datastream = _lookup_datastream_in_collection(doc, datastream_id);
	if (datastream == NULL) {
		const char* error = datastream_id ?
			oscap_sprintf("Could not find any datastream of id '%s'", datastream_id) :
			oscap_sprintf("Could not find any datastream inside the file");

		oscap_seterr(OSCAP_EFAMILY_XML, error);
		oscap_free(error);
		xmlFreeDoc(doc);
		return 1;
	}

	char* mangled_path = ds_sds_mangle_filepath(new_component);

	char* cref_id = oscap_sprintf("scap_org.open-scap_cref_%s", mangled_path);
	oscap_free(mangled_path);
	if (ds_sds_compose_add_component_with_ref(doc, datastream, new_component, cref_id) != 0) {
		oscap_free(cref_id);
		return 1;
	}
	oscap_free(cref_id);

	if (xmlSaveFileEnc(target_datastream, doc, "utf-8") == -1)
	{
		oscap_seterr(OSCAP_EFAMILY_GLIBC, "Error saving source datastream to '%s'.", target_datastream);
		xmlFreeDoc(doc);
		return 1;
	}
	xmlFreeDoc(doc);
	return 0;
}

int ds_sds_compose_from_xccdf(const char* xccdf_file, const char* target_datastream)
{
	xmlDocPtr doc = xmlNewDoc(BAD_CAST "1.0");
	xmlNodePtr root = xmlNewNode(NULL, BAD_CAST "data-stream-collection");
	xmlDocSetRootElement(doc, root);

	xmlNsPtr ds_ns = xmlNewNs(root, BAD_CAST datastream_ns_uri, BAD_CAST "ds");
	xmlSetNs(root, ds_ns);

	// we will need this namespace later when creating xlink:href attr in
	// component-ref
	xmlNewNs(root, BAD_CAST xlink_ns_uri, BAD_CAST "xlink");

	char* mangled_xccdf_file = ds_sds_mangle_filepath(xccdf_file);
	char* collection_id = oscap_sprintf("scap_org.open-scap_collection_from_xccdf_%s", mangled_xccdf_file);
	xmlSetProp(root, BAD_CAST "id", BAD_CAST collection_id);
	oscap_free(collection_id);

	xmlSetProp(root, BAD_CAST "schematron-version", BAD_CAST "1.0");

	// we will need this namespace later when creating component-ref
	// dependency catalog
	xmlNewNs(root, BAD_CAST cat_ns_uri, BAD_CAST "cat");

	xmlNodePtr datastream = xmlNewNode(ds_ns, BAD_CAST "data-stream");
	xmlAddChild(root, datastream);

	char* datastream_id = oscap_sprintf("scap_org.open-scap_datastream_from_xccdf_%s", mangled_xccdf_file);
	xmlSetProp(datastream, BAD_CAST "id", BAD_CAST datastream_id);
	oscap_free(datastream_id);

	xmlSetProp(datastream, BAD_CAST "scap-version", BAD_CAST "1.2");

	xmlSetProp(datastream, BAD_CAST "use-case", BAD_CAST "OTHER");

	xmlNodePtr dictionaries = xmlNewNode(ds_ns, BAD_CAST "dictionaries");
	xmlAddChild(datastream, dictionaries);

	xmlNodePtr checklists = xmlNewNode(ds_ns, BAD_CAST "checklists");
	xmlAddChild(datastream, checklists);

	xmlNodePtr checks = xmlNewNode(ds_ns, BAD_CAST "checks");
	xmlAddChild(datastream, checks);

	xmlNodePtr extended_components = xmlNewNode(ds_ns, BAD_CAST "extended-components");
	xmlAddChild(datastream, extended_components);

	char* cref_id = oscap_sprintf("scap_org.open-scap_cref_%s", mangled_xccdf_file);
	if (ds_sds_compose_add_component_with_ref(doc, datastream, xccdf_file, cref_id) != 0)
	{
		// oscap_seterr already called
		oscap_free(cref_id);
		oscap_free(mangled_xccdf_file);
		return -1;
	}
	oscap_free(cref_id);

	// the XSD of source data stream enforces that the collection elements are
	// not empty if they are there, we will therefore now removes collections
	// where nothing has been added

	if (dictionaries->children == NULL)
	{
		xmlUnlinkNode(dictionaries);
		xmlFreeNode(dictionaries);
	}
	if (checklists->children == NULL)
	{
		xmlUnlinkNode(checklists);
		xmlFreeNode(checklists);
	}
	if (checks->children == NULL)
	{
		xmlUnlinkNode(checks);
		xmlFreeNode(checks);
	}
	if (extended_components->children == NULL)
	{
		xmlUnlinkNode(extended_components);
		xmlFreeNode(extended_components);
	}

	oscap_free(mangled_xccdf_file);

	if (xmlSaveFileEnc(target_datastream, doc, "utf-8") == -1)
	{
		oscap_seterr(OSCAP_EFAMILY_GLIBC, "Error saving source datastream to '%s'.", target_datastream);
		xmlFreeDoc(doc);
		return -1;
	}

	xmlFreeDoc(doc);
	return 0;
}
