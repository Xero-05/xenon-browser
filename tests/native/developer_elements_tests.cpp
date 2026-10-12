#include "xenon/developer_elements.hpp"
#include <stdexcept>
using namespace xenon;
void require(bool ok){if(!ok)throw std::runtime_error("Developer elements regression");}
int main(){
  Json button{{"role","button"},{"name","Fixture button"},{"targetAvailable",true}};
  require(developer_element_actions(button)=="Click / key");
  button["states"]={{"disabled",true}};require(developer_element_actions(button).empty());
  button.erase("states");button["protected"]=true;require(developer_element_actions(button).empty());
  button.erase("protected");button["targetAvailable"]=false;require(developer_element_actions(button).empty());
  require(developer_element_actions({{"role","StaticText"},{"targetAvailable",true}}).empty());
  Json field{{"role","textbox"},{"targetAvailable",true}};
  require(developer_element_actions(field)=="Fill / key");
  require(developer_element_actions({{"role","combobox"},{"tag","TEXTAREA"},{"targetAvailable",true}})=="Fill / key");
  require(developer_element_actions({{"role","combobox"},{"tag","INPUT"},{"inputType","search"},{"targetAvailable",true}})=="Fill / key");
  field["states"]={{"readonly",true}};require(developer_element_actions(field)=="Focus / key");
  require(developer_element_actions({{"tag","INPUT"},{"inputType","file"},{"targetAvailable",true}})=="Upload");
  require(developer_element_actions({{"role","checkbox"},{"targetAvailable",true}})=="Check");
  require(developer_element_actions({{"tag","SELECT"},{"targetAvailable",true}})=="Select");
  DeveloperElementsIdentity identity;
  Json first{{"tab","A"},{"document","1"},{"human",0}};auto request=identity.reset(first);
  require(identity.matches(request,first));
  auto second=first;second["tab"]="B";require(!identity.matches(request,second));
  second=first;second["document"]="2";require(!identity.matches(request,second));
  second=first;second["human"]=1;require(!identity.matches(request,second));
  identity.reset(first);require(!identity.matches(request,first));
  auto next=identity.reset(second);require(identity.matches(next,second));require(!identity.matches(request,first));
}
