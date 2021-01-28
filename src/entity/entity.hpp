//
// Copyright Copyright 2009-2021, AMT – The Association For Manufacturing Technology (“AMT”)
// All rights reserved.
//
//    Licensed under the Apache License, Version 2.0 (the "License");
//    you may not use this file except in compliance with the License.
//    You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//    Unless required by applicable law or agreed to in writing, software
//    distributed under the License is distributed on an "AS IS" BASIS,
//    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//    See the License for the specific language governing permissions and
//    limitations under the License.
//

#pragma once

#include "observation/data_set.hpp"
#include "requirement.hpp"
#include <unordered_map>

namespace mtconnect
{
  namespace entity
  {
    using Properties = std::map<std::string, Value>;
    using OrderList = std::list<std::string>;
    using OrderMap = std::unordered_map<std::string, int>;
    using OrderMapPtr = std::shared_ptr<OrderMap>;
    using Property = std::pair<std::string, Value>;

    template <typename T>
    inline std::optional<T> OptionallyGet(const std::string &key, const Properties &props)
    {
      auto p = props.find(key);
      if (p != props.end())
        return std::get<T>(p->second);
      else
        return std::nullopt;
    }

    class Entity : public std::enable_shared_from_this<Entity>
    {
    public:
      Entity() {}
      Entity(const std::string &name, const Properties &props) : m_name(name), m_properties(props)
      {
      }
      Entity(const Entity &entity) = default;
      virtual ~Entity() {}

      EntityPtr getptr() { return shared_from_this(); }

      bool hasListWithAttribute() const
      {
        return (m_properties.count("LIST") > 0 && m_properties.size() > 1);
      }
      const std::string &getName() const { return m_name; }
      const Properties &getProperties() const { return m_properties; }
      const Value &getProperty(const std::string &n) const
      {
        static Value noValue{std::monostate()};
        auto it = m_properties.find(n);
        if (it == m_properties.end())
          return noValue;
        else
          return it->second;
      }
      virtual void setProperty(const std::string &key, const Value &v)
      {
        m_properties.insert_or_assign(key, v);
      }
      void setProperty(const Property &property) { setProperty(property.first, property.second); }
      bool hasProperty(const std::string &n) const
      {
        return m_properties.find(n) != m_properties.end();
      }
      void setName(const std::string &name) { m_name = name; }

      const Value &getValue() const { return getProperty("VALUE"); }
      std::optional<EntityList> getList(const std::string &name) const
      {
        auto &v = getProperty(name);
        auto *p = std::get_if<EntityPtr>(&v);
        if (p)
        {
          auto &lv = (*p)->getProperty("LIST");
          auto *l = std::get_if<EntityList>(&lv);
          if (l)
            return *l;
        }

        return std::nullopt;
      }
      void setValue(const Value &v) { setProperty("VALUE", v); }
      void erase(const std::string &name) { m_properties.erase(name); }

      template <typename T>
      const T &get(const std::string &name) const
      {
        return std::get<T>(getProperty(name));
      }

      template <typename T>
      const T &getValue() const
      {
        return std::get<T>(getValue());
      }

      template <typename T>
      const std::optional<T> maybeGet(const std::string &name) const
      {
        return OptionallyGet<T>(name, m_properties);
      }
      template <typename T>
      const std::optional<T> maybeGetValue() const
      {
        return OptionallyGet<T>("VALUE", m_properties);
      }

      void setOrder(const OrderMapPtr order) { m_order = order; }
      const OrderMapPtr getOrder() const { return m_order; }
      auto find(const std::string &name) { return m_properties.find(name); }
      auto erase(Properties::iterator &it) { return m_properties.erase(it); }

      // Entity Factory
    protected:
      std::string m_name;
      Properties m_properties;
      OrderMapPtr m_order;
    };
  }  // namespace entity
}  // namespace mtconnect
