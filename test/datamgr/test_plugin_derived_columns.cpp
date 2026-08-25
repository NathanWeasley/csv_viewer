#include "test_case.h"
#include "code_viewer/datamgr/data_manager.h"

#include <memory>
#include <string>
#include <vector>

TEST_GROUP(PluginDerivedColumns)
{

TEST(PluginDerivedColumns, ReplaceAndRemoveOwnedBatch)
{
    viewer::DataManager manager;
    TEST_ASSERT_TRUE(manager.LoadFromColumns(
        {"raw"}, {{1.0, 2.0, 3.0}}, "memory"));

    TEST_ASSERT_EQ(
        manager.PublishPluginDerivedColumns(
            "log_expand", {"expanded"}, {{10.0, 20.0, 30.0}}),
        viewer::PublishDerivedColumnsStatus::Success);
    TEST_ASSERT_EQ(manager.GetColumnCount(), 2u);
    const viewer::ColumnMetadata* metadata = manager.GetColumnMetadata(1);
    TEST_ASSERT_TRUE(metadata != nullptr);
    TEST_ASSERT_EQ(metadata->origin, viewer::ColumnOrigin::PluginDerived);
    TEST_ASSERT_EQ(metadata->ownerPluginId, std::string("log_expand"));

    const std::shared_ptr<const viewer::Column> oldSnapshot =
        manager.GetColumnShared(1);
    TEST_ASSERT_TRUE(oldSnapshot != nullptr);
    TEST_ASSERT_NEAR(oldSnapshot->getDouble(0), 10.0, 1e-12);

    std::vector<std::shared_ptr<viewer::Column>> retired;
    TEST_ASSERT_EQ(
        manager.PublishPluginDerivedColumns(
            "log_expand", {"expanded", "second"},
            {{40.0, 50.0, 60.0}, {7.0, 8.0, 9.0}}, &retired),
        viewer::PublishDerivedColumnsStatus::Success);
    TEST_ASSERT_EQ(manager.GetColumnCount(), 3u);
    TEST_ASSERT_NEAR(manager.GetColumn("expanded")->getDouble(0), 40.0, 1e-12);
    TEST_ASSERT_NEAR(oldSnapshot->getDouble(0), 10.0, 1e-12);
    TEST_ASSERT_EQ(retired.size(), 1u);

    TEST_ASSERT_EQ(
        manager.PublishPluginDerivedColumns("log_expand", {}, {}),
        viewer::PublishDerivedColumnsStatus::Success);
    TEST_ASSERT_EQ(manager.GetColumnCount(), 1u);
    TEST_ASSERT_TRUE(manager.GetColumn("raw") != nullptr);
}

TEST(PluginDerivedColumns, RejectsForeignColumnCollision)
{
    viewer::DataManager manager;
    TEST_ASSERT_TRUE(manager.LoadFromColumns(
        {"raw"}, {{1.0, 2.0}}, "memory"));

    TEST_ASSERT_EQ(
        manager.PublishPluginDerivedColumns(
            "log_expand", {"raw"}, {{3.0, 4.0}}),
        viewer::PublishDerivedColumnsStatus::NameCollision);
    TEST_ASSERT_NEAR(manager.GetColumn("raw")->getDouble(0), 1.0, 1e-12);

    TEST_ASSERT_EQ(
        manager.PublishPluginDerivedColumns(
            "other_plugin", {"other"}, {{5.0, 6.0}}),
        viewer::PublishDerivedColumnsStatus::Success);
    TEST_ASSERT_EQ(
        manager.PublishPluginDerivedColumns(
            "log_expand", {"other"}, {{7.0, 8.0}}),
        viewer::PublishDerivedColumnsStatus::NameCollision);
}

TEST(PluginDerivedColumns, PreservesXAxisByNameAcrossBatchReplacement)
{
    viewer::DataManager manager;
    TEST_ASSERT_TRUE(manager.LoadFromColumns(
        {"raw"}, {{1.0, 2.0}}, "memory"));
    TEST_ASSERT_EQ(
        manager.PublishPluginDerivedColumns(
            "log_expand", {"expanded"}, {{3.0, 4.0}}),
        viewer::PublishDerivedColumnsStatus::Success);
    manager.SetXAxisColumn(manager.GetColumnIndex("expanded"));

    TEST_ASSERT_EQ(
        manager.PublishPluginDerivedColumns(
            "log_expand", {"expanded", "second"},
            {{5.0, 6.0}, {7.0, 8.0}}),
        viewer::PublishDerivedColumnsStatus::Success);
    TEST_ASSERT_EQ(manager.GetColumnNames()[manager.GetXAxisColumn()],
                   std::string("expanded"));
}

} // TEST_GROUP(PluginDerivedColumns)
