/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Changes from Qualcomm Technologies, Inc. are provided under the following license:
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "camera_hidl_vendor_tag_descriptor.h"

#include "utils/camera_log.h"

using namespace android;

status_t CustomVendorTagDescriptor::createDescriptorFromHidl(
    const std::vector<VendorTagSection>& vts,
    android::sp<VendorTagDescriptor>& descriptor) {

    int tagCount = 0;

    for (size_t s = 0; s < vts.size(); s++) {
        tagCount += vts[s].tags.size();
    }

    if (tagCount < 0 || tagCount > INT32_MAX) {
        CAMERA_ERROR("%s: tag count %d from vendor tag sections is invalid.", __func__, tagCount);
        return BAD_VALUE;
    }

    std::vector<uint32_t> tagArray(tagCount);

    android::sp<CustomVendorTagDescriptor> desc = new CustomVendorTagDescriptor();
    desc->mTagCount = tagCount;

    SortedVector<String8> sections;
    KeyedVector<uint32_t, String8> tagToSectionMap;

    int idx = 0;
    for (size_t s = 0; s < vts.size(); s++) {
        const VendorTagSection& section = vts[s];
        const char *sectionName = section.sectionName.c_str();
        if (sectionName == NULL) {
            CAMERA_ERROR("%s: no section name defined for vendor tag section %zu.", __func__, s);
            return BAD_VALUE;
        }
        String8 sectionString(sectionName);
        sections.add(sectionString);

        for (size_t j = 0; j < section.tags.size(); j++) {
            uint32_t tag = section.tags[j].tagId;
            if (tag < CAMERA_METADATA_VENDOR_TAG_BOUNDARY) {
                CAMERA_ERROR("%s: vendor tag %d not in vendor tag section.", __func__, tag);
                return BAD_VALUE;
            }
            tagArray[idx++] = section.tags[j].tagId;

            const char *tagName = section.tags[j].tagName.c_str();
            if (tagName == NULL) {
                CAMERA_ERROR("%s: no tag name defined for vendor tag %d.", __func__, tag);
                return BAD_VALUE;
            }
            desc->mTagToNameMap.add(tag, String8(tagName));
            tagToSectionMap.add(tag, sectionString);

            int tagType = (int) section.tags[j].tagType;
            if (tagType < 0) {
                CAMERA_ERROR("%s: tag type %d from vendor ops does not exist.", __func__, tagType);
                return BAD_VALUE;
            }
            desc->mTagToTypeMap.insert(std::make_pair(tag, tagType));
        }
    }

    desc->mSections = sections;

    for (size_t i = 0; i < tagArray.size(); ++i) {
        uint32_t tag = tagArray[i];
        String8 sectionString = tagToSectionMap.valueFor(tag);

        ssize_t index = sections.indexOf(sectionString);
        LOG_ALWAYS_FATAL_IF(index < 0, "index %zd must be non-negative", index);
        if (index < 0) {
          CAMERA_ERROR("%s: index %zd must be non-negative", __func__, index);
          return BAD_VALUE;
        }
        desc->mTagToSectionMap.add(tag, static_cast<uint32_t>(index));

        ssize_t reverseIndex = -1;
        if ((reverseIndex = desc->mReverseMapping.indexOfKey(sectionString)) < 0) {
            KeyedVector<String8, uint32_t>* nameMapper = new KeyedVector<String8, uint32_t>();
            reverseIndex = desc->mReverseMapping.add(sectionString, nameMapper);
        }
        desc->mReverseMapping[reverseIndex]->add(desc->mTagToNameMap.valueFor(tag), tag);
    }

    descriptor = std::move(desc);
    return OK;
}

status_t CustomVendorTagDescriptor::createDescriptorFromAidl(
    const std::vector<VendorTagSection>& vts,
    android::sp<VendorTagDescriptor>& descriptor) {

    int tagCount = 0;
    for (size_t s = 0; s < vts.size(); s++) {
        tagCount += static_cast<int>(vts[s].tags.size());
    }
    if (tagCount < 0 || tagCount > INT32_MAX) {
        CAMERA_ERROR("%s: tag count %d from vendor tag sections is invalid.",
                     __func__, tagCount);
        return BAD_VALUE;
    }

    CAMERA_DEBUG("%s: Total vendor tags reported by provider = %d", __func__, tagCount);

    std::vector<uint32_t> tagArray(static_cast<size_t>(tagCount));

    android::sp<CustomVendorTagDescriptor> desc = new CustomVendorTagDescriptor();
    if (desc == nullptr) return NO_MEMORY;
    desc->mTagCount = tagCount;

    SortedVector<String8> sections;
    KeyedVector<uint32_t, String8> tagToSectionMap;
    CAMERA_DEBUG("%s: Building VendorTagDesc from AIDL vendor sections (count=%zu)",
        __func__, vts.size());
    int idx = 0;
    for (size_t s = 0; s < vts.size(); s++) {
        const VendorTagSection& section = vts[s];

        const char* sectionName = section.sectionName.c_str();
        if (sectionName == nullptr) {
            CAMERA_ERROR("%s: no section name defined for vendor tag section %zu.",
                         __func__, s);
            return BAD_VALUE;
        }
        String8 sectionString(sectionName);
        sections.add(sectionString);

        CAMERA_DEBUG("%s:Sc[%zu]: \"%s\" (tgs=%zu)",__func__, s, sectionName, section.tags.size());

        for (size_t j = 0; j < section.tags.size(); j++) {
            uint32_t tag = static_cast<uint32_t>(section.tags[j].tagId);
            if (tag < CAMERA_METADATA_VENDOR_TAG_BOUNDARY) {
                CAMERA_ERROR("%s: vendor tag %u not in vendor tag section.", __func__, tag);
                return BAD_VALUE;
            }
            tagArray[idx++] = tag;

            const char* tagName = section.tags[j].tagName.c_str();
            if (tagName == nullptr) {
                CAMERA_ERROR("%s: no tag name defined for vendor tag %u.", __func__, tag);
                return BAD_VALUE;
            }
            desc->mTagToNameMap.add(tag, String8(tagName));
            tagToSectionMap.add(tag, sectionString);

            int tagType = static_cast<int>(section.tags[j].tagType);
            if (tagType < 0) {
                CAMERA_ERROR("%s: tag type %d from vendor ops does not exist.",
                             __func__, tagType);
                return BAD_VALUE;
            }
            desc->mTagToTypeMap.insert(std::make_pair(tag, tagType));

            CAMERA_DEBUG("%s:• Tag[%zu]: id=%u (0x%08x)  name=\"%s\"  type=%d",__func__, j, tag,
                tag, tagName, tagType);
        }
    }

    desc->mSections = sections;

    for (size_t i = 0; i < tagArray.size(); ++i) {
        uint32_t tag = tagArray[i];
        String8 sectionString = tagToSectionMap.valueFor(tag);

        ssize_t index = sections.indexOf(sectionString);
        LOG_ALWAYS_FATAL_IF(index < 0, "index %zd must be non-negative", index);
        if (index < 0) {
            CAMERA_ERROR("%s: index %zd must be non-negative", __func__, index);
            return BAD_VALUE;
        }
        desc->mTagToSectionMap.add(tag, static_cast<uint32_t>(index));

        ssize_t reverseIndex = desc->mReverseMapping.indexOfKey(sectionString);
        if (reverseIndex < 0) {
            auto* nameMapper = new KeyedVector<String8, uint32_t>();
            reverseIndex = desc->mReverseMapping.add(sectionString, nameMapper);
        }
        desc->mReverseMapping[reverseIndex]->add(desc->mTagToNameMap.valueFor(tag), tag);

        CAMERA_DEBUG("%s:sectionIndex(\"%s\") = %zd  (tag id=%u, name=\"%s\")", __func__,
            sectionString.string(), index, tag, desc->mTagToNameMap.valueFor(tag).string());

    }

    CAMERA_DEBUG("%s: VendorTagDescriptor (AIDL) build complete. sections=%zu, tags=%zu", __func__,
        sections.size(), tagArray.size());

    descriptor = std::move(desc);
    return OK;
}
