#ifndef ATLAS_HPP
#define ATLAS_HPP
#include "TIPL/tipl.hpp"
#include <vector>
#include <string>
class atlas{
private:
    tipl::image<3,uint32_t> I;
    std::vector<uint32_t> region_value;
    std::vector<uint16_t> value2index;
    std::vector<std::string> labels;
    tipl::matrix<4,4> T;
    bool in_template_space = false;
    void load_label(void);
    size_t get_index(tipl::vector<3,float> atlas_space);
private:// for talairach only
    std::vector<std::vector<size_t> > index2label;
    std::vector<std::vector<size_t> > label2index;
private:// for multiple roi atlas only
    tipl::image<4,char> multiple_I;
    std::vector<uint32_t> multiple_I_pos;
public:
    std::string name,filename,error_msg;
    bool is_multiple_roi;
    tipl::matrix<4,4> template_to_mni;
public:
    bool load_from_file(void);
    const std::vector<std::string>& get_list(void)
    {
        if(labels.empty())
        {
            load_label();
            if(labels.empty())
                load_from_file();
        }
        return labels;
    }
    const std::vector<uint32_t>& get_num(void)
    {
        if(labels.empty())
        {
            load_label();
            if(labels.empty())
                load_from_file();
        }
        return region_value;
    }
    bool is_labeled_as(const tipl::vector<3,float>& template_space,unsigned int region_index);
    int region_index_at(const tipl::vector<3,float>& template_space);
    void region_indices_at(const tipl::vector<3,float>& template_space,std::vector<uint16_t>& indices);
};

#endif // ATLAS_HPP
