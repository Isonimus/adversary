/**
 * @file test_portal_template.cpp
 * @brief Unit tests for PortalTemplateManager
 */

#include <unity.h>
#include "modules/ap/portal_template_manager.h"
#include <cstring>

using namespace ap;

void setUp() {
    // Reset instance between tests if possible
    // Note: PortalTemplateManager doesn't have a reset() method, 
    // but we can call scanTemplates() to re-initialize.
}

void tearDown() {
}

void test_template_metadata_integrity() {
    PortalTemplate tmpl;
    tmpl.setName("Test Template");
    tmpl.setPath("/sd/templates/test");
    tmpl.hasCSS = true;
    tmpl.hasLogo = false;
    tmpl.isBuiltIn = false;
    
    TEST_ASSERT_EQUAL_STRING("Test Template", tmpl.name);
    TEST_ASSERT_EQUAL_STRING("/sd/templates/test", tmpl.path);
    TEST_ASSERT_TRUE(tmpl.hasCSS);
    TEST_ASSERT_FALSE(tmpl.hasLogo);
    TEST_ASSERT_FALSE(tmpl.isBuiltIn);
}

void test_manager_initialization() {
    PortalTemplateManager& mgr = PortalTemplateManager::getInstance();
    
    // Initial state might be true or false depending on previous tests, 
    // but scanTemplates() should set it to true.
    mgr.scanTemplates();
    TEST_ASSERT_TRUE(mgr.isInitialized());
    
    // Should have Generic and Google templates (built-in)
    TEST_ASSERT_TRUE(mgr.getTemplateCount() >= 2);
}

void test_get_template_by_name() {
    PortalTemplateManager& mgr = PortalTemplateManager::getInstance();
    mgr.scanTemplates();
    
    const PortalTemplate* google = mgr.getTemplateByName("Google");
    TEST_ASSERT_NOT_NULL(google);
    TEST_ASSERT_EQUAL_STRING("Google", google->name);
    TEST_ASSERT_TRUE(google->isBuiltIn);
    
    const PortalTemplate* missing = mgr.getTemplateByName("NonExistent");
    TEST_ASSERT_NULL(missing);
}

void test_template_retrieval_by_index() {
    PortalTemplateManager& mgr = PortalTemplateManager::getInstance();
    mgr.scanTemplates();
    
    size_t count = mgr.getTemplateCount();
    TEST_ASSERT_TRUE(count > 0);
    
    const PortalTemplate* first = mgr.getTemplate(0);
    TEST_ASSERT_NOT_NULL(first);
    
    const PortalTemplate* outOfBounds = mgr.getTemplate(count);
    TEST_ASSERT_NULL(outOfBounds);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_template_metadata_integrity);
    RUN_TEST(test_manager_initialization);
    RUN_TEST(test_get_template_by_name);
    RUN_TEST(test_template_retrieval_by_index);
    return UNITY_END();
}
