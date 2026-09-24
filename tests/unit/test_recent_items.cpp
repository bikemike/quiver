#include <catch2/catch_test_macros.hpp>
#include "RecentItems.h"
#include "ImageListAttributes.h"
#include <string>
#include <list>

static ImageListAttributesPtr make_attrs(std::initializer_list<const char*> folders, bool recursive)
{
	ImageListAttributesPtr attrs(new ImageListAttributes());
	for (const char* f : folders)
	{
		attrs->AddFolder(f);
	}
	attrs->SetRecursive(recursive);
	return attrs;
}

TEST_CASE("ImageListAttributes definition comparison", "[unit][recent][fast]")
{
	SECTION("Matches compares folder set and recursive flag")
	{
		ImageListAttributes a;
		a.AddFolder("file:///a");
		a.AddFolder("file:///b");
		a.SetRecursive(true);

		ImageListAttributes b;
		b.AddFolder("file:///a");
		b.AddFolder("file:///b");
		b.SetRecursive(true);

		ImageListAttributes c;
		c.AddFolder("file:///a");
		c.AddFolder("file:///b");

		ImageListAttributes d;
		d.AddFolder("file:///b");
		d.AddFolder("file:///a");

		REQUIRE(a.Matches(b));
		REQUIRE_FALSE(a.Matches(c));           // recursive differs
		REQUIRE_FALSE(a.Matches(d.GetFolders(), d.GetRecursive())); // order matters
	}

	SECTION("IsEmpty reflects empty folder set")
	{
		ImageListAttributes empty;
		REQUIRE(empty.IsEmpty());
		empty.AddFolder("file:///x");
		REQUIRE_FALSE(empty.IsEmpty());
	}

	SECTION("AddFolder dedupes")
	{
		ImageListAttributes a;
		a.AddFolder("file:///x");
		a.AddFolder("file:///x");
		REQUIRE(a.GetFolders().size() == 1);
	}
}

TEST_CASE("RecentItems MRU recording", "[unit][recent][fast]")
{
	SECTION("Initial state is empty")
	{
		RecentItems recent;
		REQUIRE(recent.GetSize() == 0);
		REQUIRE(recent.Find("file:///a.jpg") == NULL);
		REQUIRE_FALSE(recent.RemoveByURI("file:///a.jpg"));
	}

	SECTION("Record appends newest-first")
	{
		RecentItems recent;
		recent.Record("file:///a.jpg", make_attrs({"file:///f1"}, false));
		recent.Record("file:///b.jpg", make_attrs({"file:///f2"}, true));

		REQUIRE(recent.GetSize() == 2);
		REQUIRE(recent.GetEntries()[0].uri == "file:///b.jpg");
		REQUIRE(recent.GetEntries()[1].uri == "file:///a.jpg");
	}

	SECTION("Re-recording dedupes and moves to front")
	{
		RecentItems recent;
		recent.Record("file:///a.jpg", make_attrs({"file:///f1"}, false));
		recent.Record("file:///b.jpg", make_attrs({"file:///f2"}, false));
		recent.Record("file:///a.jpg", make_attrs({"file:///f1"}, false));

		REQUIRE(recent.GetSize() == 2);
		REQUIRE(recent.GetEntries()[0].uri == "file:///a.jpg");
		REQUIRE(recent.GetEntries()[1].uri == "file:///b.jpg");
	}

	SECTION("Cap trims the oldest entries")
	{
		RecentItems recent(3);
		recent.Record("file:///1.jpg", make_attrs({"file:///f"}, false));
		recent.Record("file:///2.jpg", make_attrs({"file:///f"}, false));
		recent.Record("file:///3.jpg", make_attrs({"file:///f"}, false));
		recent.Record("file:///4.jpg", make_attrs({"file:///f"}, false));

		REQUIRE(recent.GetSize() == 3);
		REQUIRE(recent.GetEntries()[0].uri == "file:///4.jpg");
		REQUIRE(recent.Find("file:///1.jpg") == NULL);
	}

	SECTION("Entries from the same list share the attributes pointer")
	{
		RecentItems recent;
		ImageListAttributesPtr shared = make_attrs({"file:///family"}, true);
		recent.Record("file:///a.jpg", shared);
		recent.Record("file:///b.jpg", shared);

		REQUIRE(recent.Find("file:///a.jpg")->pAttributes == shared);
		REQUIRE(recent.Find("file:///b.jpg")->pAttributes == shared);
	}

	SECTION("Find updates nothing; entries keep newest-first order")
	{
		RecentItems recent;
		recent.Record("file:///a.jpg", make_attrs({"file:///f"}, false));
		recent.Record("file:///b.jpg", make_attrs({"file:///f"}, false));
		REQUIRE(recent.Find("file:///a.jpg") != NULL);
		REQUIRE(recent.GetEntries()[0].uri == "file:///b.jpg");
	}
}

TEST_CASE("RecentItems deletion pruning", "[unit][recent][fast]")
{
	SECTION("RemoveAllByURIs removes matching entries")
	{
		RecentItems recent;
		recent.Record("file:///a.jpg", make_attrs({"file:///f1"}, false));
		recent.Record("file:///b.jpg", make_attrs({"file:///f2"}, false));
		recent.Record("file:///c.jpg", make_attrs({"file:///f3"}, false));

		std::list<std::string> deleted = {"file:///a.jpg", "file:///c.jpg"};
		REQUIRE(recent.RemoveAllByURIs(deleted) == 2);
		REQUIRE(recent.GetSize() == 1);
		REQUIRE(recent.Find("file:///a.jpg") == NULL);
		REQUIRE(recent.Find("file:///c.jpg") == NULL);
		REQUIRE(recent.Find("file:///b.jpg") != NULL);
	}

	SECTION("RemoveAllByURIs is a no-op for unknown URIs")
	{
		RecentItems recent;
		recent.Record("file:///a.jpg", make_attrs({"file:///f"}, false));
		std::list<std::string> deleted = {"file:///zzz.jpg"};
		REQUIRE(recent.RemoveAllByURIs(deleted) == 0);
		REQUIRE(recent.GetSize() == 1);
	}

	SECTION("RemoveByURI returns success only when found")
	{
		RecentItems recent;
		recent.Record("file:///a.jpg", make_attrs({"file:///f"}, false));
		REQUIRE(recent.RemoveByURI("file:///a.jpg"));
		REQUIRE(recent.GetSize() == 0);
		REQUIRE_FALSE(recent.RemoveByURI("file:///a.jpg"));
	}

	SECTION("SetMaxEntries trims")
	{
		RecentItems recent(10);
		recent.Record("file:///1.jpg", make_attrs({"file:///f"}, false));
		recent.Record("file:///2.jpg", make_attrs({"file:///f"}, false));
		recent.Record("file:///3.jpg", make_attrs({"file:///f"}, false));
		recent.SetMaxEntries(2);
		REQUIRE(recent.GetSize() == 2);
		REQUIRE(recent.Find("file:///3.jpg") != NULL);
		REQUIRE(recent.Find("file:///1.jpg") == NULL);
	}

	SECTION("Empty or attribute-less records are ignored")
	{
		RecentItems recent;
		recent.Record("", make_attrs({"file:///f"}, false));
		recent.Record("file:///x.jpg", ImageListAttributesPtr());
		REQUIRE(recent.GetSize() == 0);
	}
}