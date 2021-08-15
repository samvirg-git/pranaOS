/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// includes
#include <libweb/dom/DocumentFragment.h>
#include <libweb/html/HTMLElement.h>

namespace Web::HTML {

class HTMLTemplateElement final : public HTMLElement {
public:
    using WrapperType = Bindings::HTMLTemplateElementWrapper;

    HTMLTemplateElement(DOM::Document&, QualifiedName);
    virtual ~HTMLTemplateElement() override;

    NonnullRefPtr<DOM::DocumentFragment> content() { return *m_content; }
    const NonnullRefPtr<DOM::DocumentFragment> content() const { return *m_content; }

    virtual void adopted_from(DOM::Document&) override;
    virtual void cloned(Node& copy, bool clone_children) override;

private:
    DOM::Document& appropriate_template_contents_owner_document(DOM::Document&);

    RefPtr<DOM::DocumentFragment> m_content;
};

}
