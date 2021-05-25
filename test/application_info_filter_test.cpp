/* Copyright (c) 2019 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "fossilize_application_filter.hpp"
#include "layer/utils.hpp"
#include "vulkan.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool write_string_to_file(const char *path, const char *str)
{
	FILE *file = fopen(path, "w");
	if (!file)
		return false;

	fprintf(file, "%s\n", str);
	fclose(file);
	return true;
}

int main()
{
	const char *test_json =
R"delim(
{
	"asset": "FossilizeApplicationInfoFilter",
	"version" : 1,
	"blacklistedApplicationNames" : [ "A",  "B", "C" ],
	"blacklistedEngineNames" : [ "D", "E", "F" ],
	"applicationFilters" : {
		"test1" : { "minimumApplicationVersion" : 10 },
		"test2" : { "minimumApplicationVersion" : 10, "minimumEngineVersion" : 1000 },
		"test3" : { "minimumApiVersion" : 50 },
		"test4" : {
			"blacklistedEnvironments" : {
				"TEST_ENV" : { "contains" : "foo", "equals" : "bar" },
				"TEST_ENV" : { "equals" : "bar2", "contains": "" },
				"TEST" : { "nonnull" : true }
			}
		}
	},
	"engineFilters" : {
		"test1" : { "minimumEngineVersion" : 10 },
		"test2" : { "minimumEngineVersion" : 10, "minimumApplicationVersion" : 1000 },
		"test3" : { "minimumApiVersion" : 50 },
		"test4" : {
			"blacklistedEnvironments" : {
				"TEST_ENV" : { "contains" : "foo", "equals" : "bar" },
				"TEST_ENV" : { "equals" : "bar2", "contains": "" },
				"TEST" : { "nonnull" : true }
			}
		}
	}
}
)delim";

	if (!write_string_to_file(".__test_appinfo.json", test_json))
		return EXIT_FAILURE;

	Fossilize::ApplicationInfoFilter filter;
	filter.parse_async(".__test_appinfo.json");

	if (!filter.check_success())
	{
		LOGE("Parsing did not complete successfully.\n");
		return EXIT_FAILURE;
	}

	VkApplicationInfo appinfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };

	if (!filter.test_application_info(nullptr))
		return EXIT_FAILURE;

	// Test blacklists
	appinfo.pApplicationName = "A";
	appinfo.pEngineName = "G";
	if (filter.test_application_info(&appinfo))
		return EXIT_FAILURE;

	appinfo.pApplicationName = "D";
	appinfo.pEngineName = "A";
	if (!filter.test_application_info(&appinfo))
		return EXIT_FAILURE;

	appinfo.pApplicationName = "H";
	appinfo.pEngineName = "E";
	if (filter.test_application_info(&appinfo))
		return EXIT_FAILURE;

	// Test application version filtering
	appinfo.pApplicationName = "test1";
	appinfo.pEngineName = nullptr;
	appinfo.applicationVersion = 9;
	if (filter.test_application_info(&appinfo))
		return EXIT_FAILURE;
	appinfo.applicationVersion = 10;
	if (!filter.test_application_info(&appinfo))
		return EXIT_FAILURE;

	// Engine version should be ignored for appinfo filters.
	appinfo.pApplicationName = "test2";
	if (!filter.test_application_info(&appinfo))
		return EXIT_FAILURE;

	appinfo.pApplicationName = "test3";
	appinfo.applicationVersion = 0;
	appinfo.apiVersion = 49;
	if (filter.test_application_info(&appinfo))
		return EXIT_FAILURE;

	appinfo.apiVersion = 50;
	if (!filter.test_application_info(&appinfo))
		return EXIT_FAILURE;

	// Test engine version filtering
	appinfo.pApplicationName = nullptr;
	appinfo.pEngineName = "test1";
	appinfo.engineVersion = 9;
	if (filter.test_application_info(&appinfo))
		return EXIT_FAILURE;
	appinfo.engineVersion = 10;
	if (!filter.test_application_info(&appinfo))
		return EXIT_FAILURE;

	// Engine version should be ignored for appinfo filters.
	appinfo.pEngineName = "test2";
	if (!filter.test_application_info(&appinfo))
		return EXIT_FAILURE;

	appinfo.pEngineName = "test3";
	appinfo.engineVersion = 0;
	appinfo.apiVersion = 49;
	if (filter.test_application_info(&appinfo))
		return EXIT_FAILURE;

	appinfo.apiVersion = 50;
	if (!filter.test_application_info(&appinfo))
		return EXIT_FAILURE;

	appinfo.engineVersion = 0;
	appinfo.applicationVersion = 0;

	for (unsigned i = 0; i < 2; i++)
	{
		// Test env blacklisting (application)
		if (i == 0)
		{
			appinfo.pApplicationName = "test4";
			appinfo.pEngineName = nullptr;
		}
		else
		{
			appinfo.pEngineName = "test4";
			appinfo.pApplicationName = nullptr;
		}

		struct UserData
		{
			const char *env = nullptr;
			const char *data = nullptr;
		} data = {};

		const auto getenv_wrapper = +[](const char *env, void *userdata) -> const char *
		{
			auto *env_data = static_cast<const UserData *>(userdata);
			if (env_data->env && strcmp(env_data->env, env) == 0)
				return env_data->data;
			else
				return nullptr;
		};

		filter.set_environment_resolver(getenv_wrapper, &data);
		if (!filter.test_application_info(&appinfo))
			return EXIT_FAILURE;

		data.env = "TEST_FOO";
		data.data = "foo";

		if (!filter.test_application_info(&appinfo))
			return EXIT_FAILURE;

		// Contains test, should fail
		data.env = "TEST_ENV";

		data.data = "foo";
		if (filter.test_application_info(&appinfo))
			return EXIT_FAILURE;

		data.data = "Afoo";
		if (filter.test_application_info(&appinfo))
			return EXIT_FAILURE;

		data.data = "fooA";
		if (filter.test_application_info(&appinfo))
			return EXIT_FAILURE;

		// Equals bar, should fail
		data.data = "bar";
		if (filter.test_application_info(&appinfo))
			return EXIT_FAILURE;

		// Equals bar2, should fail
		data.data = "bar2";
		if (filter.test_application_info(&appinfo))
			return EXIT_FAILURE;

		// Should not fail
		data.data = "bar3";
		if (!filter.test_application_info(&appinfo))
			return EXIT_FAILURE;

		// Should not fail
		data.env = "TEST";
		data.data = nullptr;
		if (!filter.test_application_info(&appinfo))
			return EXIT_FAILURE;

		// Should fail
		data.data = "";
		if (filter.test_application_info(&appinfo))
			return EXIT_FAILURE;
	}

	remove(".__test_appinfo.json");
}
