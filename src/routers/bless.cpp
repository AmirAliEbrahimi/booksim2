#include <string>
#include <sstream>
#include <iostream>
#include <cstdlib>
#include <cassert>
#include <limits>

#include "bless.hpp"
#include "stats.hpp"
#include "globals.hpp"
#include "routefunc.hpp"

Bless::Bless(const Configuration &config,
			 Module *parent, const string &name, int id,
			 int inputs, int outputs)
	: Router(config,
			 parent, name,
			 id,
			 inputs, outputs)
{
	ostringstream module_name;

	// Routing
	string rf = "dor_next_mesh";
	map<string, cRoutingFunction>::iterator rf_iter = cRoutingFunctionMap.find(rf);
	if (rf_iter == cRoutingFunctionMap.end())
	{
		Error("Invalid routing function: " + rf);
	}
	_rf = rf_iter->second;

	assert(_inputs == _outputs);

	_input_buffer.resize(_inputs - 1);
	_output_buffer.resize(_outputs - 1);

	_stage_1.resize(_inputs - 1);

	_time = 0;
	_inject_slot = -1;
	last_channel = _inputs - 1;
}

Bless::~Bless()
{
	for (int i = 0; i < _inputs - 1; ++i)
	{
		while (!_input_buffer[i].empty())
		{
			(_input_buffer[i].begin()->second)->Free();
			_input_buffer[i].erase(_input_buffer[i].begin());
		}
	}

	for (int i = 0; i < _inputs - 1; ++i)
	{
		while (!_stage_1[i].empty())
		{
			(_stage_1[i].begin()->second)->Free();
			_stage_1[i].erase(_stage_1[i].begin());
		}
	}

	for (int o = 0; o < _outputs - 1; ++o)
	{
		while (!_output_buffer[o].empty())
		{
			(_output_buffer[o].begin()->second)->Free();
			_output_buffer[o].erase(_output_buffer[o].begin());
		}
	}
}

void Bless::AddInputChannel(FlitChannel *channel, CreditChannel *ignored)
{
	_input_channels.push_back(channel);
	channel->SetSink(this, _input_channels.size() - 1);
}

void Bless::AddOutputChannel(FlitChannel *channel, CreditChannel *ignored)
{
	_output_channels.push_back(channel);
	_channel_faults.push_back(false);
	channel->SetSource(this, _output_channels.size() - 1);
}

void Bless::Display(ostream &os) const
{
	os << "Nothing to display" << endl; //Just for sake of avoiding pure virual func
}

//Returns 1 if there is empty slot in _stage_1
int Bless::GetInjectStatus()
{
	return (_inject_queue.empty());
}

//Performs the function of reading flits from channel into input buffer
void Bless::ReadInputs()
{
	int time = GetSimTime();
	Flit *f;
	for (int input = 0; input < _inputs - 1; ++input) //	Avoid _inject channel
	{
		f = _input_channels[input]->Receive();

		if (f)
		{
			if (f->watch)
			{
				*gWatchOut << GetSimTime() << " | "
						   << "node" << GetID() << " | "
						   << "Flit " << f->id
						   << " arrived at input " << input << " | "
						   << "destination " << f->dest
						   << "." << endl;
			}
			_input_buffer[input].insert(pair<int, Flit *>(time, f));
		}
	}
}

void Bless::WriteOutputs()
{
	_SendFlits(); //Sending flits from output buffer into input channel, no credits sent, as in event_router
}

//Definition of _Sendflits in Chipper class same as that in class event_router
void Bless::_SendFlits()
{
	int time = GetSimTime();
	map<int, Flit *>::iterator f;
	for (int output = 0; output < _outputs - 1; ++output)
	{
		map<int, Flit *> &buffer_timed = _output_buffer[output];
		f = buffer_timed.find(time);
		if (f != buffer_timed.end())
		{
			if ((f->second)->watch)
			{
				*gWatchOut << GetSimTime() << " | "
						   << "node" << GetID() << " | "
						   << "Flit " << (f->second)->id
						   << " sent from output " << output << " | "
						   << "destination " << (f->second)->dest
						   << "." << endl;
			}
			(f->second)->hops++;
			_output_channels[output]->Send(f->second);
			_output_channels[output]->ReadInputs();
			buffer_timed.erase(f);
		}
	}
}

void Bless::QueueFlit(Flit *f)
{
	_inject_queue.push(f);
}

void Bless::_InternalStep()
{
	_time = GetSimTime();

	_EjectFlits();

	_input_to_stage1();

	_routing();

	_stage1_to_output();

	CheckSanity();

	WriteOutputs();
}

void Bless::_EjectFlits()
{
	Flit *received_flits[_inputs - 1];
	Flit *f = NULL;
	map<int, Flit *>::iterator it;
	int flit_to_eject = -1;
	for (int input = 0; input < _inputs - 1; ++input)
	{
		it = _input_buffer[input].find(_time);
		if (it == _input_buffer[input].end())
		{
			received_flits[input] = NULL;
			continue;
		}
		f = it->second;
		if (f->dest == GetID())
		{
			if (f->watch)
			{
				*gWatchOut << GetSimTime() << " | "
						   << "node" << GetID() << " | "
						   << "Flit " << f->id
						   << " waiting for eject at " << f->dest
						   << " with priority " << f->pri
						   << "." << endl;
			}
			received_flits[input] = f;
			flit_to_eject = input;
		}
		else
		{
			received_flits[input] = NULL; // Flit not at destination
		}
	}

	if (flit_to_eject != -1 && f)
	{
		if (f->watch)
		{
			*gWatchOut << GetSimTime() << " | "
					   << "node" << GetID() << " | "
					   << "Flit " << f->id
					   << " accepted for eject at " << f->dest
					   << "." << endl;
		}
		_output_channels[last_channel]->Send(received_flits[flit_to_eject]);
		_input_buffer[flit_to_eject].erase(_input_buffer[flit_to_eject].find(_time));
	}
}

void Bless::_input_to_stage1()
{
	map<int, Flit *>::iterator it;
	for (int input = 0; input < _inputs - 1; ++input)
	{
		it = _input_buffer[input].find(_time);
		if (it == _input_buffer[input].end())
		{
			if (_inject_slot == -1)
			{
				_inject_slot = input;
			}
			continue;
		}
		if ((it->second)->watch)
		{
			*gWatchOut << GetSimTime() << " | "
					   << "node" << GetID() << " | "
					   << "Flit " << (it->second)->id
					   << " headed for " << (it->second)->dest
					   << " written from _input_buffer to _stage_1 in slot "
					   << input
					   << "." << endl;
		}
		_stage_1[input].insert(make_pair(it->first + 1, it->second));
		_input_buffer[input].erase(it);
	}

	if ((_inject_slot > -1) && (!_inject_queue.empty()))
	{
		assert(_inject_slot < _inputs - 1);
		if (_stage_1[_inject_slot].size() == 0)
		{
			Flit *f = _inject_queue.front();
			_inject_queue.pop();

			f->itime = _time;
			f->pri = numeric_limits<int>::max() - f->itime;

			if (f)
			{
				if (f->watch)
				{
					*gWatchOut << GetSimTime() << " | "
							   << "node" << GetID() << " | "
							   << "Receiving flit " << f->id
							   << " headed for " << f->dest
							   << " at time " << _time
							   << " at slot " << _inject_slot
							   << " with priority " << f->pri
							   << "." << endl;
				}
				_stage_1[_inject_slot].insert(make_pair(_time + 1, f));
			}
		}
	}

	_inject_slot = -1;
}

void Bless::_routing()
{
	map<int, Flit *>::iterator it;
	int maxPri = -1;
	Flit *maxPriF = NULL;
	int inpNum = 0;
	int maxPri_output_number;
	int output_number;
	Flit *tmp;
	for (int input = 0; input < _inputs - 1; ++input)
	{
		it = _stage_1[input].find(_time);
		if (it == _stage_1[input].end())
			continue;
		if ((it->second)->pri > maxPri)
		{
			maxPriF = it->second;
			maxPri = maxPriF->pri;
			inpNum = input;
		}
	}
	if (maxPriF)
	{
		maxPri_output_number = _rf(GetID(), maxPriF->dest, true) % 4;
		if (_stage_1[maxPri_output_number].count(_time) == 0)
		{
			_stage_1[maxPri_output_number].insert(pair<int, Flit *>(_time, maxPriF));
			_stage_1[inpNum].erase(_stage_1[inpNum].find(_time));
		}
		else
		{
			tmp = _stage_1[maxPri_output_number].find(_time)->second;
			if (tmp != maxPriF)
			{
				_stage_1[maxPri_output_number].erase(_stage_1[maxPri_output_number].find(_time));
				_stage_1[maxPri_output_number].insert(pair<int, Flit *>(_time, maxPriF));
				_stage_1[inpNum].erase(_stage_1[inpNum].find(_time));
				_stage_1[inpNum].insert(pair<int, Flit *>(_time, tmp));
			}
		}
	}
	for (int input = 0; input < _inputs - 1; ++input)
	{
		it = _stage_1[input].find(_time);
		if (it == _stage_1[input].end())
			continue;
		if ((it->second) == maxPriF)
			continue;
		output_number = _rf(GetID(), it->second->dest, true) % 4;
		if (_stage_1[output_number].count(_time) == 0)
		{
			_stage_1[output_number].insert(pair<int, Flit *>(_time, it->second));
			_stage_1[input].erase(_stage_1[input].find(_time));
		}
		else if (_stage_1[output_number].find(_time)->second->pri < it->second->pri)
		{
			tmp = _stage_1[output_number].find(_time)->second;
			_stage_1[output_number].erase(_stage_1[output_number].find(_time));
			_stage_1[output_number].insert(pair<int, Flit *>(_time, it->second));
			_stage_1[input].erase(_stage_1[input].find(_time));
			_stage_1[input].insert(pair<int, Flit *>(_time, tmp));
		}
	}
}

void Bless::_stage1_to_output()
{
	map<int, Flit *>::iterator it;
	for (int input = 0; input < _inputs - 1; ++input)
	{
		it = _stage_1[input].find(_time);
		if (it == _stage_1[input].end())
			continue;
		if ((it->second)->watch)
		{
			*gWatchOut << GetSimTime() << " | "
					   << "node" << GetID() << " | "
					   << "Flit " << (it->second)->id
					   << " headed for " << (it->second)->dest
					   << " written from _stage_1 to _output_buffer in slot "
					   << input
					   << "." << endl;
		}
		_output_buffer[input].insert(make_pair(it->first + 1, it->second));
		_stage_1[input].erase(it);
	}
}

void Bless::CheckSanity()
{
	for (int i = 0; i < _inputs - 1; ++i)
	{
		if (_input_buffer[i].size() > 2)
		{
			ostringstream err;
			err << "Flit pile up at input buffer of router: " << GetID();
			Error(err.str());
		}
	}

	for (int i = 0; i < _inputs - 1; ++i)
	{
		if (_stage_1[i].size() > 2)
		{
			ostringstream err;
			err << "Flit pile up at _stage_1 of router: " << GetID();
			Error(err.str());
		}
	}

	for (int o = 0; o < _outputs - 1; ++o)
	{
		if (_output_buffer[o].size() > 2)
		{
			ostringstream err;
			err << "Flit pile up at output buffer of router: " << GetID();
			Error(err.str());
		}
	}
}
