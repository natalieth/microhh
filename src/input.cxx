/*
 * MicroHH
 * Copyright (c) 2011-2015 Chiel van Heerwaarden
 * Copyright (c) 2011-2015 Thijs Heus
 * Copyright (c) 2014-2015 Bart van Stratum
 *
 * This file is part of MicroHH
 *
 * MicroHH is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * MicroHH is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with MicroHH.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <cstdio>
#include <cstring>
#include <cctype>
#include <map>
#include "input.h"
#include "master.h"
#include <algorithm>
#include <string>
#include <iostream>
#include <iomanip>
#include <sstream>

// Public functions
Input::Input(Master* masterin)
{
    master = masterin;

    const bool required = false;
    const bool optional = true;

    int nerror = 0;
    nerror += read_ini_file();
    nerror += read_data_file(&proflist, master->simname + ".prof", required);
    nerror += read_data_file(&timelist, master->simname + ".time", optional);

    if (nerror)
        throw 1;
}

Input::~Input()
{
}

void Input::clear()
{
    inputlist.clear();
    proflist.clear();
}

// Private functions
int Input::read_ini_file()
{
    int nerror = 0;
    char inputline[256], temp1[256], block[256], lhs[256], rhs[256], dummy[256], element[256];

    // read the input file
    FILE *inputfile = 0;
    std::string inputfilename = master->simname + ".ini";

    if (master->mpiid == 0)
    {
        inputfile = fopen(inputfilename.c_str(), "r");
        if (inputfile == NULL)
        {
            std::printf("ERROR \"%s\" does not exist\n", inputfilename.c_str());
            ++nerror;
        }
    }

    // broadcast the error count
    master->broadcast(&nerror, 1);
    if (nerror)
        return 1;

    int n;
    bool blockset = false;
    int nerrors = 0;
    int nlines  = 0;
    int nline;

    if (master->mpiid == 0)
    {
        std::printf("Processing ini file \"%s\"\n", inputfilename.c_str());
        while (std::fgets(inputline, 256, inputfile) != NULL)
            nlines++;
        rewind(inputfile);
    }
    master->broadcast(&nlines, 1);

    // check the cases: comments, empty line, block, value, rubbish
    for (int nn=0; nn<nlines; nn++)
    {
        nline = nn+1;
        if (master->mpiid == 0)
        {
            // fetch a line and broadcast it
            std::fgets(inputline, 256, inputfile);
        }
        master->broadcast(inputline, 256);

        // check for empty line
        n = std::sscanf(inputline, " %s ", temp1);
        if (n == 0)
            continue;

        // check for comments
        n = std::sscanf(inputline, " #%[^\n]", temp1);
        if (n > 0)
            continue;

        n = std::sscanf(inputline, " [%[^]]] ", temp1);
        if (n == 1)
        {
            n = std::sscanf(temp1, "%s %s", block, dummy);
            if (n == 1)
            {
                blockset = true;
            }
            else
            {
                if (master->mpiid == 0) std::printf("ERROR line %d: illegal block specification [%s]\n", nline, temp1);
                return 1;
            }
            continue;
        }
        // read items
        n = std::sscanf(inputline, "%[^=] = %[^\n]", temp1, rhs);
        if (n == 2)
        {

            n = std::sscanf(temp1, " %[a-zA-Z0-9_()][%[^]]] %s", lhs, element, dummy);
            if (n <= 2)
            {
                if (!blockset)
                {
                    if (master->mpiid == 0) std::printf("ERROR line %d: illegal item [?][%s] = \"%s\"\n", nline, lhs, rhs);
                    nerrors++;
                    return 1;
                }

                if (n ==1)
                {
                    std::strcpy(element,"default");
                }
                std::string blockstring(block);
                std::string itemstring(lhs);
                std::string elementstring(element);
                std::string valuestring(rhs);
                if (check_item_exists(blockstring, itemstring, elementstring))
                {
                    inputlist[blockstring][itemstring][elementstring].data   = valuestring;
                    inputlist[blockstring][itemstring][elementstring].isused = false;
                }
                else
                {
                    if (master->mpiid == 0) std::printf("ERROR line %d: Item [%s][%s][%s] defined for the second time\n", nline, block, lhs, element);
                    return 1;
                }
            }
            else
            {
                n = std::sscanf(inputline, "%[^=]", temp1);
                if (master->mpiid == 0) std::printf("ERROR line %d: illegal item  [%s][%s]\n", nline, block, temp1);
                nerrors++;
            }
        }

        // throw exception
        else
        {
            n = std::sscanf(inputline, "%[^\n]", temp1);
            if (n > 0)
            {
                if (master->mpiid == 0) std::printf("ERROR line %d: \"%s\" is illegal input\n", nline, temp1);
                nerrors++;
            }
        }
    }

    if (master->mpiid == 0)
        fclose(inputfile);

    return nerrors;
}

int Input::read_data_file(Data_map* series, std::string inputname, bool optional)
{
    int nerror = 0;
    char inputline[256], temp1[256];
    char* substring;
    int n;

    // read the input file
    FILE* inputfile = 0;
    std::string inputfilename = inputname;

    int doreturn = 0;
    if (master->mpiid == 0)
    {
        inputfile = fopen(inputfilename.c_str(), "r");
        if (inputfile == NULL)
        {
            if (optional)
                doreturn = true;
            else
            {
                std::printf("ERROR \"%s\" does not exist\n", inputfilename.c_str());
                nerror++;
            }
        }
    }

    // broadcast the error count
    master->broadcast(&nerror  , 1);
    master->broadcast(&doreturn, 1);
    if (nerror)
        return 1;
    if (doreturn)
        return 0;

    int nlines = 0;
    int nline;
    int nvar = 0;
    std::vector<std::string> varnames;

    if (master->mpiid == 0)
    {
        std::printf("Processing data file \"%s\"\n", inputfilename.c_str());
        while (std::fgets(inputline, 256, inputfile) != NULL)
            nlines++;
        rewind(inputfile);
    }
    master->broadcast(&nlines, 1);

    int nn;

    // first find the header
    for (nn=0; nn<nlines; nn++)
    {
        nline = nn+1;
        if (master->mpiid == 0)
        {
            // fetch a line and broadcast it
            std::fgets(inputline, 256, inputfile);
        }
        master->broadcast(inputline, 256);

        // check for empty line
        n = std::sscanf(inputline, " %s ", temp1);
        if (n == 0)
            continue;

        // check for comments
        n = std::sscanf(inputline, " #%[^\n]", temp1);
        if (n > 0)
            continue;

        // read the header
        // read the first substring
        substring = std::strtok(inputline, " ,;\t\n");
        while (substring != NULL)
        {
            nvar++;

            // CvH remove in order to make time step reading possible
            /*
               if (!std::isalpha(substring[0]))
               {
               if (master->mpiid == 0)
               {
               std::printf("ERROR at line %d: \"%s\" is not a variable name\n", nline, substring);
               fclose(inputfile);
               }
               return 1;
               }
               */

            // temporarily store the variable name
            varnames.push_back(std::string(substring));

            // read the next substring
            substring = std::strtok(NULL, " ,;\t\n");
        }

        if (nvar == 0)
        {
            if (master->mpiid == 0)
            {
                std::printf("ERROR no variable names in header\n");
                fclose(inputfile);
            }
            return 1;
        }

        // step out of the fgets loop
        break;
    }

    // second read the data
    // continue reading
    int ncols;
    double datavalue;

    std::vector<double> varvalues;

    // continue the loop from the exit value of nn
    for (nn++; nn<nlines; nn++)
    {
        nline = nn+1;
        if (master->mpiid == 0)
        {
            // fetch a line and broadcast it
            std::fgets(inputline, 256, inputfile);
        }
        master->broadcast(inputline, 256);

        // check for empty line
        n = std::sscanf(inputline, " %s ", temp1);
        if (n == 0)
            continue;

        // check for comments
        n = std::sscanf(inputline, " #%[^\n]", temp1);
        if (n > 0)
            continue;

        // read the data
        ncols = 0;
        varvalues.clear();
        // read the first substring
        substring = std::strtok(inputline, " ,;\t\n");
        while (substring != NULL)
        {
            ncols++;

            // scan the line, while checking that the whole string has been read
            n = std::sscanf(substring, " %lf %[^\n]", &datavalue, temp1);

            if (n != 1)
            {
                if (master->mpiid == 0)
                {
                    std::printf("ERROR line %d: \"%s\" is not a correct data value\n", nline, substring);
                    fclose(inputfile);
                }
                return 1;
            }

            // temporarily store the data
            varvalues.push_back(datavalue);

            // read the next substring
            substring = std::strtok(NULL, " ,;\t\n");
        }

        if (ncols != nvar)
        {
            if (master->mpiid == 0)
            {
                std::printf("ERROR line %d: %d data columns, but %d defined variables\n", nline, ncols, nvar);
                fclose(inputfile);
            }
            return 1;
        }

        // store the data
        for (n=0; n<nvar; n++)
            (*series)[varnames[n]].push_back(varvalues[n]);
    }

    if (master->mpiid == 0)
        fclose(inputfile);

    return 0;
}

int Input::check_item_exists(std::string cat, std::string item, std::string el)
{
    Input_map::const_iterator it1 = inputlist.find(cat);

    bool readerror = false;

    if (it1 != inputlist.end())
    {
        Input_map_2d::const_iterator it2 = it1->second.find(item);

        if (it2 != it1->second.end())
        {
            Input_map_1d::const_iterator it3 = it2->second.find(el);
            if (it3 == it2->second.end())
                readerror = true;
        }
        else
            readerror = true;
    }
    else
        readerror = true;

    if (readerror)
        return 1;

    return 0;
}

// overloaded return functions
// int functions
int Input::get_item(int* value, std::string cat, std::string item, std::string el)
{
    bool optional = false;
    int dummy = 0;

    if (parse_item(value, cat, item, el, optional, dummy))
        return 1;

    return 0;
}

int Input::get_item(int* value, std::string cat, std::string item, std::string el, int def)
{
    bool optional = true;

    if (parse_item(value, cat, item, el, optional, def))
        return 1;

    return 0;
}

template <class valuetype>
int Input::parse_item(valuetype* value, std::string cat, std::string item, std::string el, bool optional, valuetype def)
{
    std::string itemout, itemtype;
    itemout = "[" + cat + "][" + item + "]";

    if (!el.empty())
    {
        itemout += "[" + el + "]";
        if (!check_item_exists(cat, item, el))
        {
            if (check_item(value, cat, item, el))
                return 1;
            itemtype = "(element specific)";
        }
    }
    if (itemtype.empty())
    {
        if (check_item_exists(cat, item))
        {
            if (optional)
            {
                *value = def;
                itemtype = "(default)";
            }
            else
            {
                if (master->mpiid == 0) std::printf("ERROR [%s][%s] does not exist\n", cat.c_str(), item.c_str());
                return 1;
            }
        }
        else
        {
            if (check_item(value, cat, item))
                return 1;
            itemtype = "(global)";
        }
    }
    if (master->mpiid == 0) 
        std::cout << std::left  << std::setw(30) << itemout << "= " 
            << std::right << std::setw(11) << std::setprecision(5) << std::boolalpha << *value 
            << "   " << itemtype << std::endl;

    return 0;
}

int Input::check_item(int* value, std::string cat, std::string item, std::string el)
{
    char inputstring[256], temp[256];
    std::strcpy(inputstring, inputlist[cat][item][el].data.c_str());

    int inputint;
    int n = std::sscanf(inputstring, " %d %[^\n] ", &inputint, temp);

    if (n == 1)
        *value = inputint;
    else
    {
        if (std::strcmp(inputstring,""))
        {
            if (el == "default")
            {
                if (master->mpiid == 0) std::printf("ERROR [%s][%s] = \"%s\" is not of type INT\n", cat.c_str(), item.c_str(), inputstring);
            }
            else
            {
                if (master->mpiid == 0) std::printf("ERROR [%s][%s][%s] = \"%s\" is not of type INT\n", cat.c_str(), item.c_str(), el.c_str(), inputstring);
            }
            return 1;
        }
    }
    inputlist[cat][item][el].isused = true;

    return 0;
}

// double functions
int Input::get_item(double* value, std::string cat, std::string item, std::string el)
{
    bool optional = false;
    double dummy = 0.;

    if (parse_item(value, cat, item, el, optional, dummy))
        return 1;

    return 0;
}

int Input::get_item(double* value, std::string cat, std::string item, std::string el, double def)
{
    bool optional = true;

    if (parse_item(value, cat, item, el, optional, def))
        return 1;

    return 0;
}

int Input::check_item(double* value, std::string cat, std::string item, std::string el)
{
    char inputstring[256], temp[256];
    std::strcpy(inputstring, inputlist[cat][item][el].data.c_str());

    double inputdouble;
    int n = std::sscanf(inputstring, " %lf %[^\n] ", &inputdouble, temp);
    // catch the situation where a double is closed with a ".", which is not read by sscanf's %f
    if (n == 1 || (n == 2 && !std::strcmp(".", temp)))
        *value = inputdouble;
    else
    {
        if (std::strcmp(inputstring,""))
        {
            if (el == "default")
            {
                if (master->mpiid == 0) std::printf("ERROR [%s][%s] = \"%s\" is not of type DOUBLE\n", cat.c_str(), item.c_str(), inputstring);
            }
            else
            {
                if (master->mpiid == 0) std::printf("ERROR [%s][%s][%s] = \"%s\" is not of type DOUBLE\n", cat.c_str(), item.c_str(), el.c_str(), inputstring);
            }
            return 1;
        }
    }
    inputlist[cat][item][el].isused = true;

    return 0;
}

// booleans
int Input::get_item(bool* value, std::string cat, std::string item, std::string el)
{
    bool optional = false;
    bool dummy = false;

    if (parse_item(value, cat, item, el, optional, dummy))
        return 1;
    return 0;
}

int Input::get_item(bool* value, std::string cat, std::string item, std::string el, bool def)
{
    bool optional = true;

    if (parse_item(value, cat, item, el, optional, def))
        return 1;

    return 0;
}

int Input::check_item(bool* value, std::string cat, std::string item, std::string el)
{
    char inputstring[256], inputbool[256], temp[256];
    std::strcpy(inputstring, inputlist[cat][item][el].data.c_str());

    int n = std::sscanf(inputstring, " %s %[^\n] ", inputbool, temp);

    bool boolerror = false;

    if (n == 1)
    {
        if (std::strcmp("true", inputbool) == 0 ||
                std::strcmp("1"   , inputbool) == 0 )
            *value = true;
        else if (std::strcmp("false", inputbool) == 0 ||
                std::strcmp("0    ", inputbool) == 0 )
            *value = false;
        else
            boolerror = true;
    }

    if (n != 1 || boolerror)
    {
        if (std::strcmp(inputstring,""))
        {
            if (el == "default")
            {
                if (master->mpiid == 0) std::printf("ERROR [%s][%s] = \"%s\" is not of type BOOL\n", cat.c_str(), item.c_str(), inputstring);
            }
            else
            {
                if (master->mpiid == 0) std::printf("ERROR [%s][%s][%s] = \"%s\" is not of type BOOL\n", cat.c_str(), item.c_str(), el.c_str(), inputstring);
            }
            return 1;
        }
    }
    inputlist[cat][item][el].isused = true;

    return 0;
}

// strings
int Input::get_item(std::string* value, std::string cat, std::string item, std::string el)
{
    bool optional = false;
    std::string dummy = "";

    if (parse_item(value, cat, item, el, optional, dummy))
        return 1;

    return 0;
}

int Input::get_item(std::string* value, std::string cat, std::string item, std::string el, std::string def)
{
    bool optional = true;

    if (parse_item(value, cat, item, el, optional, def))
        return 1;

    return 0;
}

int Input::check_item(std::string* value, std::string cat, std::string item, std::string el)
{
    char inputstring[256], stringval[256], dummy[256];
    std::strcpy(inputstring, inputlist[cat][item][el].data.c_str());

    int n = std::sscanf(inputstring, " %s %[^\n] ", stringval, dummy);

    if (n == 1)
        *value = stringval;
    else
    {
        if (std::strcmp(inputstring,""))
        {
            if (el == "default")
            {
                if (master->mpiid == 0) std::printf("ERROR [%s][%s] = \"%s\" is not of type STRING\n", cat.c_str(), item.c_str(), inputstring);
            }
            else
            {
                if (master->mpiid == 0) std::printf("ERROR [%s][%s][%s] = \"%s\" is not of type STRING\n", cat.c_str(), item.c_str(), el.c_str(), inputstring);
            }
            return 1;
        }
    }
    inputlist[cat][item][el].isused = true;

    return 0;
}

// list retrieval function
int Input::get_list(std::vector<int>* value, std::string cat, std::string item, std::string el)
{
    if (parse_list(value, cat, item, el))
        return 1;

    return 0;
}

int Input::get_list(std::vector<double>* value, std::string cat, std::string item, std::string el)
{
    if (parse_list(value, cat, item, el))
        return 1;

    return 0;
}

int Input::get_list(std::vector<std::string>* value, std::string cat, std::string item, std::string el)
{
    if (parse_list(value, cat, item, el))
        return 1;

    return 0;
}

    template <class valuetype>
int Input::parse_list(std::vector<valuetype>* value, std::string cat, std::string item, std::string el)
{
    std::string itemout, listout;
    std::stringstream liststream;

    itemout = "[" + cat + "][" + item + "]";
    if (check_item_exists(cat, item))
    {
        if (master->mpiid == 0)
            std::cout << std::left  << std::setw(30) << itemout << "= "
                << std::right << std::setw(11) << "EMPTY LIST" << std::endl;
    }
    else
    {
        if (check_list(value, cat, item))
            return 1;
        typedef typename std::vector<valuetype>::iterator itertype;
        for (itertype it = value->begin(); it !=value->end()-1; ++it)
        {
            liststream << *it << ", ";
        }
        liststream << *(value->end()-1);
        if (master->mpiid == 0)
            std::cout << std::left  << std::setw(30) << itemout << "= "
                << std::right << std::setw(11) << liststream.str() << std::endl;
    }

    return 0;
}

int Input::check_list(std::vector<std::string>* value, std::string cat, std::string item, std::string el)
{
    char inputstring[256], dummy[256];
    std::strcpy(inputstring, inputlist[cat][item][el].data.c_str());

    char temp1[256];
    char* temp2;
    std::strcpy(temp1, inputstring);

    // first, split string on the delimiter
    temp2 = std::strtok(temp1, ",");

    while (temp2 != NULL)
    {
        // read in the string part in temp1
        int n = std::sscanf(temp2, "%s %s", temp1, dummy);

        // store the contents in the vector, or throw exception
        if (n == 1)
            value->push_back(temp1);
        else
        {
            if (std::strcmp(inputstring,""))
            {
                if (el == "default")
                {
                    if (master->mpiid == 0) std::printf("ERROR [%s][%s] = \"%s\" is not a list of type STRING\n", cat.c_str(), item.c_str(), inputstring);
                }
                else
                {
                    if (master->mpiid == 0) std::printf("ERROR [%s][%s][%s] = \"%s\" is not a list of type STRING\n", cat.c_str(), item.c_str(), el.c_str(), inputstring);
                }
                // empty the vector
                value->clear();
                return 1;
            }
        }

        // retrieve the next raw substring
        temp2 = std::strtok(NULL, ",");
    }
    inputlist[cat][item][el].isused = true;

    return 0;
}

int Input::check_list(std::vector<int>* value, std::string cat, std::string item, std::string el)
{
    char inputstring[256], dummy[256];
    std::strcpy(inputstring, inputlist[cat][item][el].data.c_str());

    int listval;

    char temp1[256];
    char* temp2;
    std::strcpy(temp1, inputstring);

    // first, split string on the delimiter
    temp2 = std::strtok(temp1, ",");

    while (temp2 != NULL)
    {
        // read in the string part in temp1
        int n = std::sscanf(temp2, " %d %[^\n] ", &listval, dummy);

        // store the contents in the vector, or throw exception
        if (n == 1)
            value->push_back(listval);
        else
        {
            if (std::strcmp(inputstring,""))
            {
                if (el == "default")
                {
                    if (master->mpiid == 0) std::printf("ERROR [%s][%s] = \"%s\" is not a list of type INT\n", cat.c_str(), item.c_str(), inputstring);
                }
                else
                {
                    if (master->mpiid == 0) std::printf("ERROR [%s][%s][%s] = \"%s\" is not a list of type INT\n", cat.c_str(), item.c_str(), el.c_str(), inputstring);
                }
                // empty the vector
                value->clear();
                return 1;
            }
        }

        // retrieve the next raw substring
        temp2 = std::strtok(NULL, ",");
    }
    inputlist[cat][item][el].isused = true;

    return 0;
}

int Input::check_list(std::vector<double>* value, std::string cat, std::string item, std::string el)
{
    char inputstring[256], dummy[256];
    std::strcpy(inputstring, inputlist[cat][item][el].data.c_str());

    double listval;

    char temp1[256];
    char* temp2;
    std::strcpy(temp1, inputstring);

    // first, split string on the delimiter
    temp2 = std::strtok(temp1, ",");

    while (temp2 != NULL)
    {
        // read in the string part in temp1
        int n = std::sscanf(temp2, " %lf %[^\n] ", &listval, dummy);

        // store the contents in the vector, or throw exception
        if (n == 1)
        {
            value->push_back(listval);
        }
        else
        {
            if (std::strcmp(inputstring,""))
            {
                if (el == "default")
                {
                    if (master->mpiid == 0) std::printf("ERROR [%s][%s] = \"%s\" is not a list of type DOUBLE\n", cat.c_str(), item.c_str(), inputstring);
                }
                else
                {
                    if (master->mpiid == 0) std::printf("ERROR [%s][%s][%s] = \"%s\" is not a list of type DOUBLE\n", cat.c_str(), item.c_str(), el.c_str(), inputstring);
                }
                // empty the vector
                value->clear();
                return 1;
            }
        }

        // retrieve the next raw substring
        temp2 = std::strtok(NULL, ",");
    }
    inputlist[cat][item][el].isused = true;

    return 0;
}

int Input::get_prof(double* data, std::string varname, int kmaxin)
{
    Data_map::const_iterator it = proflist.find(varname);

    if (it != proflist.end())
    {
        int profsize = proflist[varname].size();
        if (profsize < kmaxin)
        {
            if (master->mpiid == 0) std::printf("ERROR only %d of %d levels can be read for variable \"%s\"\n", profsize, kmaxin, varname.c_str());
            return 1;
        }
        if (profsize > kmaxin)
            if (master->mpiid == 0) std::printf("WARNING %d is larger than the number of grid points %d for variable \"%s\"\n", profsize, kmaxin, varname.c_str());

        for (int k=0; k<kmaxin; k++)
            data[k] = proflist[varname][k];
    }
    else
    {
        if (master->mpiid == 0) std::printf("WARNING no profile data for variable \"%s\", values set to zero\n", varname.c_str());
        for (int k=0; k<kmaxin; k++)
            data[k] = 0.;
    }

    return 0;
}

int Input::get_time(double** data, std::vector<double>* time, std::string varname)
{
    // first get the time list
    Data_map::const_iterator it = timelist.find("t");
    if (it != timelist.end())
        *time = it->second;
    else
    {
        if (master->mpiid == 0) std::printf("ERROR no header \"t\" found\n");
        return 1;
    }

    // next, find the data
    it = timelist.find(varname);
    if (it != timelist.end())
    {
        unsigned int timesize = timelist[varname].size();
        if (timesize != time->size())
        {
            if (master->mpiid == 0) std::printf("ERROR number of values does not match number of time entries\n");
            return 1;
        }
        // allocate the data
        *data = new double[timesize];
        for (unsigned int n=0; n<timesize; ++n)
            (*data)[n] = (it->second)[n];
    }
    else
    {
        if (master->mpiid == 0) std::printf("ERROR no time data found for variable \"%s\"\n", varname.c_str());
        return 1;
    }

    return 0;
}

int Input::get_time_prof(double** timeprof, std::vector<double>* timelist, std::string varname, int kmaxin)
{
    // container for the raw data
    Data_map rawdata;

    // create a typedef to store the time in string and double to allow sorting
    typedef std::map<double, std::string> Time_map;
    Time_map rawtimemap;

    // read the file that contains the time varying data
    if (read_data_file(&rawdata, varname + ".timeprof", false))
        return 1;

    // delete the column with the profile data
    rawdata.erase("z");

    // allocate the 2d array containing the profiles
    *timeprof = new double[rawdata.size()*kmaxin];

    // first process the headers in order to get the time series
    int timecount = 0;
    for (Data_map::const_iterator it=rawdata.begin(); it!=rawdata.end(); ++it)
    {
        // check whether the item name is of type double
        char inputstring[256], temp[256];
        std::strcpy(inputstring, it->first.c_str());
        double timedouble;
        int n = std::sscanf(inputstring, " %lf %[^\n] ", &timedouble, temp);
        if (n == 1 || (n == 2 && !std::strcmp(".", temp)))
            rawtimemap[timedouble] = it->first;
        else
        {
            if (master->mpiid == 0) std::printf("ERROR header item \"%s\" is not of type DOUBLE\n", it->first.c_str());
            return 1;
        }
    }

    // now loop over the new time list in the correct order (sort on double rather than string)
    for (Time_map::const_iterator it=rawtimemap.begin(); it!=rawtimemap.end(); ++it)
    {
        int profsize = rawdata[it->second].size();
        if (profsize < kmaxin)
        {
            if (master->mpiid == 0) std::printf("ERROR only %d of %d levels can be read for header item \"%s\"\n", profsize, kmaxin, varname.c_str());
            return 1;
        }
        if (profsize > kmaxin)
            if (master->mpiid == 0) std::printf("WARNING %d is larger than the number of grid points %d for header item \"%s\"\n", profsize, kmaxin, varname.c_str());

        // all checks passed, save the data now
        // save the time data
        timelist->push_back(it->first);

        // save the profile
        for (int k=0; k<kmaxin; k++)
            (*timeprof)[timecount*kmaxin + k] = rawdata[it->second][k];

        ++timecount;
    }

    return 0;
}

void Input::print_unused()
{
    for (Input_map::iterator it1=inputlist.begin(); it1!=inputlist.end(); ++it1)
        for (Input_map_2d::iterator it2=it1->second.begin(); it2!=it1->second.end(); ++it2)
            for (Input_map_1d::iterator it3=it2->second.begin(); it3!=it2->second.end(); ++it3)
            {
                if (!it3->second.isused)
                {
                    if (it3->first == "default")
                    {
                        if (master->mpiid == 0) std::printf("WARNING [%s][%s] = \"%s\" is not used\n", it1->first.c_str(), it2->first.c_str(), it3->second.data.c_str());
                    }
                    else
                    {
                        if (master->mpiid == 0) std::printf("WARNING [%s][%s][%s] = \"%s\" is not used\n", it1->first.c_str(), it2->first.c_str(), it3->first.c_str(), it3->second.data.c_str());
                    }
                }
            }
}

void Input::flag_as_used(std::string cat, std::string item)
{
    // Loop over the entire map to flag the chosen option as used.
    Input_map::iterator it1 = inputlist.find(cat);
    if (it1 != inputlist.end())
    {
        Input_map_2d::iterator it2 = it1->second.find(item);
        if (it2 != it1->second.end())
        {
            for (Input_map_1d::iterator it3=it2->second.begin(); it3!=it2->second.end(); ++it3)
                it3->second.isused = true;
        }
    }
}
